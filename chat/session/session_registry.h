#pragma once
#ifndef __CHAT_SESSION_SESSION_REGISTRY_H__
#define __CHAT_SESSION_SESSION_REGISTRY_H__

#include "chat/session/online_conn.h"
#include "include/thread.h"
#include <unordered_map>
#include <vector>

namespace chat {
namespace session {

class SessionRegistry {
public:
    typedef std::shared_ptr<SessionRegistry> ptr;
    static SessionRegistry* GetInstance();

    OnlineConn::ptr Add(const OnlineConn::ptr& conn, size_t max_devices, std::vector<OnlineConn::ptr>* evicted = nullptr);
    bool Remove(uint64_t user_id, const std::string& device_id);

    OnlineConn::ptr Get(uint64_t user_id, const std::string& device_id);
    std::vector<OnlineConn::ptr> GetByUser(uint64_t user_id);
    bool IsUserOnline(uint64_t user_id);
    size_t UserConnectionCount(uint64_t user_id);

    void Touch(uint64_t user_id, const std::string& device_id, int64_t now_ms);
    std::vector<OnlineConn::ptr> CollectTimeouted(int64_t deadline_ms);

    size_t OnlineUsers();
    size_t OnlineConnections();

private:
    flz::RWMutex m_mutex;
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> > m_conns;
};

}
}

#endif
