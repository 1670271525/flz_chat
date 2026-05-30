#include "chat/session/session_registry.h"

namespace chat {
namespace session {

SessionRegistry* SessionRegistry::GetInstance() {
    static SessionRegistry inst;
    return &inst;
}

OnlineConn::ptr SessionRegistry::Add(const OnlineConn::ptr& conn, size_t max_devices, std::vector<OnlineConn::ptr>* evicted) {
    if(!conn) {
        return nullptr;
    }
    flz::RWMutex::WriteLock lock(m_mutex);
    std::unordered_map<std::string, OnlineConn::ptr>& by_device = m_conns[conn->user_id];

    OnlineConn::ptr replaced;
    std::unordered_map<std::string, OnlineConn::ptr>::iterator same = by_device.find(conn->device_id);
    if(same != by_device.end()) {
        replaced = same->second;
        by_device.erase(same);
    }

    by_device[conn->device_id] = conn;
    while(by_device.size() > max_devices && !by_device.empty()) {
        std::unordered_map<std::string, OnlineConn::ptr>::iterator oldest = by_device.begin();
        for(std::unordered_map<std::string, OnlineConn::ptr>::iterator it = by_device.begin();
                it != by_device.end(); ++it) {
            if(it->second->login_at < oldest->second->login_at) {
                oldest = it;
            }
        }
        if(evicted) {
            evicted->push_back(oldest->second);
        }
        by_device.erase(oldest);
    }
    return replaced;
}

bool SessionRegistry::Remove(uint64_t user_id, const std::string& device_id) {
    flz::RWMutex::WriteLock lock(m_mutex);
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.find(user_id);
    if(it == m_conns.end()) {
        return false;
    }
    it->second.erase(device_id);
    if(it->second.empty()) {
        m_conns.erase(it);
    }
    return true;
}

OnlineConn::ptr SessionRegistry::Get(uint64_t user_id, const std::string& device_id) {
    flz::RWMutex::ReadLock lock(m_mutex);
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.find(user_id);
    if(it == m_conns.end()) {
        return nullptr;
    }
    std::unordered_map<std::string, OnlineConn::ptr>::iterator dit = it->second.find(device_id);
    return dit == it->second.end() ? nullptr : dit->second;
}

std::vector<OnlineConn::ptr> SessionRegistry::GetByUser(uint64_t user_id) {
    std::vector<OnlineConn::ptr> out;
    flz::RWMutex::ReadLock lock(m_mutex);
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.find(user_id);
    if(it == m_conns.end()) {
        return out;
    }
    for(std::unordered_map<std::string, OnlineConn::ptr>::iterator dit = it->second.begin();
            dit != it->second.end(); ++dit) {
        out.push_back(dit->second);
    }
    return out;
}

bool SessionRegistry::IsUserOnline(uint64_t user_id) {
    flz::RWMutex::ReadLock lock(m_mutex);
    return m_conns.find(user_id) != m_conns.end();
}

size_t SessionRegistry::UserConnectionCount(uint64_t user_id) {
    flz::RWMutex::ReadLock lock(m_mutex);
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.find(user_id);
    return it == m_conns.end() ? 0 : it->second.size();
}

void SessionRegistry::Touch(uint64_t user_id, const std::string& device_id, int64_t now_ms) {
    flz::RWMutex::ReadLock lock(m_mutex);
    std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.find(user_id);
    if(it == m_conns.end()) {
        return;
    }
    std::unordered_map<std::string, OnlineConn::ptr>::iterator dit = it->second.find(device_id);
    if(dit == it->second.end()) {
        return;
    }
    dit->second->last_active_ts.store(now_ms);
}

std::vector<OnlineConn::ptr> SessionRegistry::CollectTimeouted(int64_t deadline_ms) {
    std::vector<OnlineConn::ptr> out;
    flz::RWMutex::ReadLock lock(m_mutex);
    for(std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.begin();
            it != m_conns.end(); ++it) {
        for(std::unordered_map<std::string, OnlineConn::ptr>::iterator dit = it->second.begin();
                dit != it->second.end(); ++dit) {
            if(dit->second->last_active_ts.load() <= deadline_ms) {
                out.push_back(dit->second);
            }
        }
    }
    return out;
}

size_t SessionRegistry::OnlineUsers() {
    flz::RWMutex::ReadLock lock(m_mutex);
    return m_conns.size();
}

size_t SessionRegistry::OnlineConnections() {
    size_t total = 0;
    flz::RWMutex::ReadLock lock(m_mutex);
    for(std::unordered_map<uint64_t, std::unordered_map<std::string, OnlineConn::ptr> >::iterator it = m_conns.begin();
            it != m_conns.end(); ++it) {
        total += it->second.size();
    }
    return total;
}

}
}
