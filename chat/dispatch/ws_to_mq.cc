#include "chat/dispatch/ws_to_mq.h"
#include "chat/config/chat_config.h"
#include "chat/metrics/metrics.h"
#include "chat/mq/publisher.h"
#include "chat/protocol.h"
#include "chat/util/time_util.h"
#include "include/json_util.h"
#include "include/log.h"

namespace chat {
namespace dispatch {

static flz::Logger::ptr g_logger = FLZ_LOG_NAME("system");

WsToMqDispatcher* WsToMqDispatcher::GetInstance() {
    static WsToMqDispatcher inst;
    return &inst;
}

int WsToMqDispatcher::HandleMsgSend(uint64_t uid, const std::string& did, flz::http::WSSession::ptr session,
                                    bool has_seq, int64_t seq, const Json::Value& data) {
    std::string client_msg_id = data.get("clientMsgId", "").asString();
    int64_t conversation_id = data.get("conversationId", 0).asInt64();
    std::string content = data.get("content", "").asString();
    std::string sent_at = data.get("sentAt", "").asString();
    const Json::Value media_meta = data["mediaMeta"];
    if(sent_at.empty()) {
        sent_at = util::Iso8601Now();
    }

    const int max_bytes = config::ChatConfig::GetInstance().chat().max_text_content_bytes;
    if(client_msg_id.empty() || conversation_id <= 0 || content.empty()) {
        Protocol::SendError(session, 400, "bad msg.send", has_seq, seq);
        return 0;
    }
    if(static_cast<int>(content.size()) > max_bytes) {
        Protocol::SendError(session, 413, "content too long", has_seq, seq);
        return 0;
    }

    PendingSend pending;
    pending.sender_user_id = uid;
    pending.sender_device_id = did;
    pending.created_ms = util::NowMs();
    pending.has_seq = has_seq;
    pending.seq = seq;
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending_map[client_msg_id] = pending;
    }

    Json::Value payload;
    payload["clientMsgId"] = client_msg_id;
    payload["conversationId"] = Json::Int64(conversation_id);
    payload["senderId"] = Json::UInt64(uid);
    payload["type"] = 1;
    payload["content"] = content;
    payload["sentAt"] = sent_at;
    if(!media_meta.isNull()) {
        if(media_meta.isString()) {
            payload["mediaMeta"] = media_meta.asString();
        } else {
            payload["mediaMeta"] = flz::JsonUtil::ToString(media_meta);
        }
    }

    if(!mq::MqPublisher::GetInstance()->Publish("business.msg.persist", payload, 0)) {
        {
            std::lock_guard<std::mutex> lock(m_pending_mutex);
            m_pending_map.erase(client_msg_id);
        }
        Protocol::SendError(session, 503, "mq unavailable", has_seq, seq);
        return 0;
    }
    FLZ_LOG_INFO(g_logger) << "msg.send publish uid=" << uid
                           << " clientMsgId=" << client_msg_id
                           << " conversationId=" << conversation_id;
    return 0;
}

int WsToMqDispatcher::HandleMsgAck(uint64_t uid, flz::http::WSSession::ptr session,
                                   bool has_seq, int64_t seq, const Json::Value& data) {
    int64_t message_id = data.get("messageId", 0).asInt64();
    if(message_id <= 0) {
        Protocol::SendError(session, 400, "bad msg.ack", has_seq, seq);
        return 0;
    }
    if(!mq::MqPublisher::GetInstance()->PublishMsgAck(message_id, uid)) {
        Protocol::SendError(session, 503, "mq unavailable", has_seq, seq);
        return 0;
    }
    return 0;
}

int WsToMqDispatcher::HandleMsgRead(uint64_t uid, flz::http::WSSession::ptr session,
                                    bool has_seq, int64_t seq, const Json::Value& data) {
    (void)session;
    (void)has_seq;
    (void)seq;
    FLZ_LOG_INFO(g_logger) << "msg.read uid=" << uid
                           << " conversationId=" << data.get("conversationId", 0).asInt64()
                           << " lastReadMessageId=" << data.get("lastReadMessageId", 0).asInt64();
    return 0;
}

std::shared_ptr<PendingSend> WsToMqDispatcher::TakePending(const std::string& client_msg_id) {
    std::lock_guard<std::mutex> lock(m_pending_mutex);
    std::unordered_map<std::string, PendingSend>::iterator it = m_pending_map.find(client_msg_id);
    if(it == m_pending_map.end()) {
        return nullptr;
    }
    std::shared_ptr<PendingSend> out(new PendingSend(it->second));
    m_pending_map.erase(it);
    return out;
}

void WsToMqDispatcher::SweepPending(int64_t now_ms) {
    const int ttl_seconds = config::ChatConfig::GetInstance().chat().pending_send_ttl_seconds;
    const int64_t deadline = now_ms - static_cast<int64_t>(ttl_seconds) * 1000;
    std::lock_guard<std::mutex> lock(m_pending_mutex);
    for(std::unordered_map<std::string, PendingSend>::iterator it = m_pending_map.begin();
            it != m_pending_map.end();) {
        if(it->second.created_ms <= deadline) {
            FLZ_LOG_WARN(g_logger) << "pending timeout clientMsgId=" << it->first
                                   << " uid=" << it->second.sender_user_id;
            it = m_pending_map.erase(it);
        } else {
            ++it;
        }
    }
}

}
}
