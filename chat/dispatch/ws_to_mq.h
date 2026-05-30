#pragma once
#ifndef __CHAT_DISPATCH_WS_TO_MQ_H__
#define __CHAT_DISPATCH_WS_TO_MQ_H__

#include "http/ws_session.h"
#include <json/json.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace chat {
namespace dispatch {

struct PendingSend {
    uint64_t sender_user_id = 0;
    std::string sender_device_id;
    int64_t created_ms = 0;
    bool has_seq = false;
    int64_t seq = 0;
};

class WsToMqDispatcher {
public:
    static WsToMqDispatcher* GetInstance();

    int HandleMsgSend(uint64_t uid, const std::string& did, flz::http::WSSession::ptr session,
                      bool has_seq, int64_t seq, const Json::Value& data);
    int HandleMsgAck(uint64_t uid, flz::http::WSSession::ptr session,
                     bool has_seq, int64_t seq, const Json::Value& data);
    int HandleMsgRead(uint64_t uid, flz::http::WSSession::ptr session,
                      bool has_seq, int64_t seq, const Json::Value& data);

    std::shared_ptr<PendingSend> TakePending(const std::string& client_msg_id);
    void SweepPending(int64_t now_ms);

private:
    WsToMqDispatcher() {}

private:
    std::mutex m_pending_mutex;
    std::unordered_map<std::string, PendingSend> m_pending_map;
};

}
}

#endif
