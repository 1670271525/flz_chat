#include "chat/util/lru_idempotency.h"

namespace chat {
namespace util {

LruIdempotencyCache::LruIdempotencyCache(size_t capacity, int64_t ttl_ms)
    : m_capacity(capacity)
    , m_ttl_ms(ttl_ms) {
}

bool LruIdempotencyCache::TryMark(const std::string& key, int64_t now_ms) {
    if(key.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    Sweep(now_ms);

    std::unordered_map<std::string, std::list<Node>::iterator>::iterator it = m_index.find(key);
    if(it != m_index.end()) {
        if(it->second->expire_at_ms > now_ms) {
            return false;
        }
        m_lru.erase(it->second);
        m_index.erase(it);
    }

    Node n;
    n.key = key;
    n.expire_at_ms = now_ms + m_ttl_ms;
    m_lru.push_front(n);
    m_index[key] = m_lru.begin();

    while(m_lru.size() > m_capacity) {
        const Node& tail = m_lru.back();
        m_index.erase(tail.key);
        m_lru.pop_back();
    }
    return true;
}

void LruIdempotencyCache::Sweep(int64_t now_ms) {
    while(!m_lru.empty()) {
        std::list<Node>::iterator it = --m_lru.end();
        if(it->expire_at_ms > now_ms && m_lru.size() <= m_capacity) {
            break;
        }
        m_index.erase(it->key);
        m_lru.erase(it);
    }
}

}
}
