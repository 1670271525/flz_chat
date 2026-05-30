#include "chat/dispatch/mq_to_ws.h"
#include "chat/dispatch/ws_to_mq.h"
#include "chat/metrics/metrics.h"
#include "chat/protocol.h"
#include "chat/session/session_registry.h"
#include "chat/util/time_util.h"
#include "include/log.h"
#include <set>

namespace chat {
namespace dispatch {

static flz::Logger::ptr g_logger = FLZ_LOG_NAME("system");

MqToWsDispatcher* MqToWsDispatcher::GetInstance() {
    static MqToWsDispatcher inst;
    return &inst;
}

mq::ConsumeResult MqToWsDispatcher::OnEnvelope(const mq::Envelope& env) {
    if(env.type == "chat.msg.send") {
        return OnChatMsgSend(env);
    }
    if(env.type == "chat.msg.replay") {
        return OnChatMsgReplay(env);
    }
    if(env.type == "chat.msg.recall") {
        return OnChatMsgRecall(env);
    }
    if(env.type == "chat.friend.request") {
        return OnFriendRequest(env);
    }
    if(env.type == "chat.friend.accept") {
        return OnFriendAccept(env);
    }
    if(env.type == "chat.conversation.created") {
        return OnConversationCreated(env);
    }
    if(env.type == "chat.conversation.member_changed") {
        return OnConversationMemberChanged(env);
    }
    FLZ_LOG_INFO(g_logger) << "skip unsupported mq type=" << env.type;
    return mq::ConsumeResult::ACK;
}

void MqToWsDispatcher::PushToUser(uint64_t uid, const std::string& type, const Json::Value& data) {
    std::vector<session::OnlineConn::ptr> conns = session::SessionRegistry::GetInstance()->GetByUser(uid);
    for(size_t i = 0; i < conns.size(); ++i) {
        session::OnlineConn::ptr conn = conns[i];
        if(!conn || !conn->session) {
            continue;
        }
        flz::IOManager* iom = conn->io_manager;
        if(iom) {
            iom->schedule([conn, type, data]() {
                Protocol::SendFrame(conn->session, type, data, false, 0);
                metrics::ChatMetrics::GetInstance().IncWsOut(type);
            });
        } else {
            Protocol::SendFrame(conn->session, type, data, false, 0);
            metrics::ChatMetrics::GetInstance().IncWsOut(type);
        }
    }
}

mq::ConsumeResult MqToWsDispatcher::OnChatMsgSend(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    const std::string client_msg_id = p.get("clientMsgId", "").asString();
    const int64_t message_id = p.get("messageId", 0).asInt64();
    const uint64_t sender_id = p.get("senderId", 0).asUInt64();

    std::shared_ptr<PendingSend> pending = WsToMqDispatcher::GetInstance()->TakePending(client_msg_id);
    if(pending) {
        uint64_t latency = static_cast<uint64_t>(util::NowMs() - pending->created_ms);
        metrics::ChatMetrics::GetInstance().ObserveMsgSendLatencyMs(latency);

        std::vector<session::OnlineConn::ptr> sender_conns = session::SessionRegistry::GetInstance()->GetByUser(sender_id);
        for(size_t i = 0; i < sender_conns.size(); ++i) {
            session::OnlineConn::ptr conn = sender_conns[i];
            if(!conn || !conn->session) {
                continue;
            }
            flz::IOManager* iom = conn->io_manager;
            if(conn->device_id == pending->sender_device_id) {
                Json::Value resp;
                resp["clientMsgId"] = client_msg_id;
                resp["code"] = 200;
                resp["messageId"] = Json::Int64(message_id);
                if(iom) {
                    iom->schedule([conn, resp, pending]() {
                        Protocol::SendFrame(conn->session, "msg.send.resp", resp, pending->has_seq, pending->seq);
                        metrics::ChatMetrics::GetInstance().IncWsOut("msg.send.resp");
                    });
                } else {
                    Protocol::SendFrame(conn->session, "msg.send.resp", resp, pending->has_seq, pending->seq);
                    metrics::ChatMetrics::GetInstance().IncWsOut("msg.send.resp");
                }
            } else {
                if(iom) {
                    iom->schedule([conn, p]() {
                        Protocol::SendFrame(conn->session, "msg.new", p, false, 0);
                        metrics::ChatMetrics::GetInstance().IncWsOut("msg.new");
                    });
                } else {
                    Protocol::SendFrame(conn->session, "msg.new", p, false, 0);
                    metrics::ChatMetrics::GetInstance().IncWsOut("msg.new");
                }
            }
        }
    }

    std::set<uint64_t> pushed_users;
    if(p.isMember("receivers") && p["receivers"].isArray()) {
        for(Json::ArrayIndex i = 0; i < p["receivers"].size(); ++i) {
            uint64_t uid = p["receivers"][i].asUInt64();
            if(uid == 0 || uid == sender_id) {
                continue;
            }
            if(pushed_users.insert(uid).second) {
                PushToUser(uid, "msg.new", p);
            }
        }
    }
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnChatMsgReplay(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    uint64_t target = p.get("targetUserId", 0).asUInt64();
    if(target == 0) {
        return mq::ConsumeResult::NACK_DROP;
    }
    PushToUser(target, "msg.replay", p);
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnChatMsgRecall(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    std::set<uint64_t> users;
    uint64_t operator_id = p.get("operatorId", 0).asUInt64();
    if(operator_id > 0) {
        users.insert(operator_id);
    }
    if(p.isMember("receivers") && p["receivers"].isArray()) {
        for(Json::ArrayIndex i = 0; i < p["receivers"].size(); ++i) {
            users.insert(p["receivers"][i].asUInt64());
        }
    }
    for(std::set<uint64_t>::iterator it = users.begin(); it != users.end(); ++it) {
        if(*it > 0) {
            PushToUser(*it, "msg.recall", p);
        }
    }
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnFriendRequest(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    uint64_t to_uid = p.get("toUserId", 0).asUInt64();
    if(to_uid == 0) {
        return mq::ConsumeResult::NACK_DROP;
    }
    PushToUser(to_uid, "friend.request", p);
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnFriendAccept(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    uint64_t from_uid = p.get("fromUserId", 0).asUInt64();
    uint64_t to_uid = p.get("toUserId", 0).asUInt64();
    if(from_uid > 0) {
        PushToUser(from_uid, "friend.accept", p);
    }
    if(to_uid > 0 && to_uid != from_uid) {
        PushToUser(to_uid, "friend.accept", p);
    }
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnConversationCreated(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    if(!p.isMember("memberIds") || !p["memberIds"].isArray()) {
        return mq::ConsumeResult::NACK_DROP;
    }
    for(Json::ArrayIndex i = 0; i < p["memberIds"].size(); ++i) {
        uint64_t uid = p["memberIds"][i].asUInt64();
        if(uid > 0) {
            PushToUser(uid, "conversation.created", p);
        }
    }
    return mq::ConsumeResult::ACK;
}

mq::ConsumeResult MqToWsDispatcher::OnConversationMemberChanged(const mq::Envelope& env) {
    const Json::Value& p = env.payload;
    std::set<uint64_t> users;
    if(p.isMember("addedIds") && p["addedIds"].isArray()) {
        for(Json::ArrayIndex i = 0; i < p["addedIds"].size(); ++i) {
            users.insert(p["addedIds"][i].asUInt64());
        }
    }
    if(p.isMember("removedIds") && p["removedIds"].isArray()) {
        for(Json::ArrayIndex i = 0; i < p["removedIds"].size(); ++i) {
            users.insert(p["removedIds"][i].asUInt64());
        }
    }
    if(p.isMember("roleChanges") && p["roleChanges"].isArray()) {
        for(Json::ArrayIndex i = 0; i < p["roleChanges"].size(); ++i) {
            users.insert(p["roleChanges"][i].get("userId", 0).asUInt64());
        }
    }
    for(std::set<uint64_t>::iterator it = users.begin(); it != users.end(); ++it) {
        if(*it > 0) {
            PushToUser(*it, "conversation.member_changed", p);
        }
    }
    return mq::ConsumeResult::ACK;
}

}
}
