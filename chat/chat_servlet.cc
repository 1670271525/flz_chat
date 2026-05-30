#include "chat_servlet.h"
#include "chat/auth/jwt.h"
#include "chat/config/chat_config.h"
#include "chat/dispatch/ws_to_mq.h"
#include "chat/metrics/metrics.h"
#include "chat/mq/publisher.h"
#include "chat/protocol.h"
#include "chat/session/session_registry.h"
#include "chat/util/time_util.h"
#include "include/log.h"
#include "include/util.h"
#include <atomic>

namespace chat {

static flz::Logger::ptr g_logger = FLZ_LOG_ROOT();
static std::atomic<uint64_t> g_fallback_device_seq(0);

namespace {

void CloseSessionOnOwner(const session::OnlineConn::ptr& conn) {
    if(!conn || !conn->session) {
        return;
    }
    flz::IOManager* iom = conn->io_manager;
    if(!iom) {
        conn->session->close();
        return;
    }
    iom->schedule([conn]() {
        if(conn->session && conn->session->getSocket()) {
            conn->session->getSocket()->cancelAll();
        }
        conn->session->close();
    });
}

void SendKickedOnOwner(const session::OnlineConn::ptr& conn, const std::string& reason) {
    if(!conn || !conn->session) {
        return;
    }
    Json::Value kicked;
    kicked["reason"] = reason;
    flz::IOManager* iom = conn->io_manager;
    if(iom) {
        iom->schedule([conn, kicked]() {
            Protocol::SendFrame(conn->session, "kicked", kicked, false, 0);
        });
    } else {
        Protocol::SendFrame(conn->session, "kicked", kicked, false, 0);
    }
}

}

ChatWSServlet::ChatWSServlet()
    :flz::http::WSServlet("chat_servlet") {
}

std::string ChatWSServlet::ExtractToken(flz::http::HttpRequest::ptr header) const {
    std::string token = header->getParam("token");
    if(!token.empty()) {
        return token;
    }

    std::string auth = header->getHeader("Authorization");
    static const std::string kBearer = "Bearer ";
    if(auth.compare(0, kBearer.size(), kBearer) == 0 && auth.size() > kBearer.size()) {
        return auth.substr(kBearer.size());
    }

    std::string subprotocol = header->getHeader("Sec-WebSocket-Protocol");
    static const std::string kProtoPrefix = "bearer.";
    if(subprotocol.compare(0, kProtoPrefix.size(), kProtoPrefix) == 0 && subprotocol.size() > kProtoPrefix.size()) {
        return subprotocol.substr(kProtoPrefix.size());
    }
    return "";
}

std::string ChatWSServlet::AllocFallbackDeviceId(uint64_t uid) const {
    uint64_t seq = ++g_fallback_device_seq;
    return "device-" + std::to_string(uid) + "-" + std::to_string(seq);
}


int32_t ChatWSServlet::onConnect(flz::http::HttpRequest::ptr header
                              ,flz::http::WSSession::ptr session) {
    std::string token = ExtractToken(header);
    auth::JwtClaims claims;
    std::string err;
    if(token.empty() || !auth::JwtVerifier::Verify(token, claims, err)) {
        metrics::ChatMetrics::GetInstance().IncJwtVerifyFail(err.empty() ? "empty_token" : err);
        Json::Value data;
        data["code"] = 401;
        data["msg"] = err.empty() ? "missing token" : err;
        Protocol::SendFrame(session, "auth_fail", data, false, 0);
        session->close();
        FLZ_LOG_WARN(g_logger) << "auth fail err=" << err;
        return -1;
    }

    std::string device_id = claims.device_id.empty() ? AllocFallbackDeviceId(claims.user_id) : claims.device_id;
    const bool first_device = !session::SessionRegistry::GetInstance()->IsUserOnline(claims.user_id);
    int64_t now_ms = util::NowMs();

    session::OnlineConn::ptr conn(new session::OnlineConn(
        claims.user_id, device_id, session, flz::IOManager::GetThis(), now_ms,
        config::ChatConfig::GetInstance().chat().frame_rate_limit_per_second));
    std::vector<session::OnlineConn::ptr> evicted;
    session::OnlineConn::ptr replaced = session::SessionRegistry::GetInstance()->Add(
        conn, static_cast<size_t>(config::ChatConfig::GetInstance().chat().max_devices_per_user), &evicted);
    if(replaced) {
        SendKickedOnOwner(replaced, "login_elsewhere");
        CloseSessionOnOwner(replaced);
    }
    for(size_t i = 0; i < evicted.size(); ++i) {
        SendKickedOnOwner(evicted[i], "too_many_devices");
        CloseSessionOnOwner(evicted[i]);
    }

    header->setHeader("$uid", std::to_string(claims.user_id));
    header->setHeader("$did", device_id);
    header->setHeader("$exp", std::to_string(claims.exp));

    Json::Value ok;
    ok["userId"] = Json::UInt64(claims.user_id);
    ok["deviceId"] = device_id;
    ok["serverTime"] = util::Iso8601Now();
    Protocol::SendFrame(session, "auth_ok", ok, false, 0);
    metrics::ChatMetrics::GetInstance().IncWsOut("auth_ok");

    if(first_device) {
        mq::MqPublisher::GetInstance()->PublishUserOnline(claims.user_id);
    }
    FLZ_LOG_INFO(g_logger) << "auth ok uid=" << claims.user_id << " did=" << device_id;
    return 0;
}

int32_t ChatWSServlet::onClose(flz::http::HttpRequest::ptr header
                             ,flz::http::WSSession::ptr session) {
    (void)session;
    uint64_t uid = flz::TypeUtil::Atoi(header->getHeader("$uid"));
    std::string did = header->getHeader("$did");
    if(uid == 0 || did.empty()) {
        return 0;
    }
    session::SessionRegistry::GetInstance()->Remove(uid, did);
    if(!session::SessionRegistry::GetInstance()->IsUserOnline(uid)) {
        mq::MqPublisher::GetInstance()->PublishUserOffline(uid);
    }
    FLZ_LOG_INFO(g_logger) << "onClose uid=" << uid << " did=" << did;
    return 0;
}

int32_t ChatWSServlet::handle(flz::http::HttpRequest::ptr header
                           ,flz::http::WSFrameMessage::ptr msgx
                           ,flz::http::WSSession::ptr session) {
    if(msgx->getOpcode() != flz::http::WSFrameHead::TEXT_FRAME) {
        Protocol::SendError(session, 400, "text frame required");
        return 1;
    }
    uint64_t uid = flz::TypeUtil::Atoi(header->getHeader("$uid"));
    std::string did = header->getHeader("$did");
    if(uid == 0 || did.empty()) {
        Protocol::SendError(session, 401, "unauthorized");
        return 1;
    }

    session::OnlineConn::ptr conn = session::SessionRegistry::GetInstance()->Get(uid, did);
    int64_t now_ms = util::NowMs();
    if(conn && !conn->ConsumeToken(now_ms, config::ChatConfig::GetInstance().chat().frame_rate_limit_per_second)) {
        Protocol::SendError(session, 429, "too many frames");
        return 1;
    }
    session::SessionRegistry::GetInstance()->Touch(uid, did, now_ms);

    ClientFrame frame;
    std::string err;
    if(!Protocol::ParseClientFrame(msgx->getData(), frame, err)) {
        Protocol::SendError(session, 400, err);
        return 0;
    }
    metrics::ChatMetrics::GetInstance().IncWsIn(frame.type);

    if(frame.type == "ping") {
        Json::Value pong_data;
        pong_data["ts"] = frame.data.get("ts", Json::Int64(now_ms));
        Protocol::SendFrame(session, "pong", pong_data, frame.has_seq, frame.seq);
        metrics::ChatMetrics::GetInstance().IncWsOut("pong");
        return 0;
    }
    if(frame.type == "msg.send") {
        return dispatch::WsToMqDispatcher::GetInstance()->HandleMsgSend(uid, did, session,
                frame.has_seq, frame.seq, frame.data);
    }
    if(frame.type == "msg.ack") {
        return dispatch::WsToMqDispatcher::GetInstance()->HandleMsgAck(uid, session,
                frame.has_seq, frame.seq, frame.data);
    }
    if(frame.type == "msg.read") {
        return dispatch::WsToMqDispatcher::GetInstance()->HandleMsgRead(uid, session,
                frame.has_seq, frame.seq, frame.data);
    }
    if(frame.type == "bye") {
        return 1;
    }
    Protocol::SendError(session, 404, "unknown type", frame.has_seq, frame.seq);
    return 0;
}

}
