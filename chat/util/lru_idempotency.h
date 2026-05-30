#pragma once
#ifndef __CHAT_UTIL_LRU_IDEMPOTENCY_H__
#define __CHAT_UTIL_LRU_IDEMPOTENCY_H__

#include <list>
#include <string>
#include <unordered_map>
#include <stdint.h>
#include <mutex>

namespace chat {
namespace util {

class LruIdempotencyCache {
public:
    LruIdempotencyCache(size_t capacity, int64_t ttl_ms);

    bool TryMark(const std::string& key, int64_t now_ms);
    void Sweep(int64_t now_ms);

private:
    struct Node {
        std::string key;
        int64_t expire_at_ms = 0;
    };

private:
    size_t m_capacity;
    int64_t m_ttl_ms;
    std::list<Node> m_lru;
    std::unordered_map<std::string, std::list<Node>::iterator> m_index;
    std::mutex m_mutex;
};

}
}

#endif
