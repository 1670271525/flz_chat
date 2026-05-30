#pragma once
#ifndef __CHAT_SESSION_ONLINE_CONN_H__
#define __CHAT_SESSION_ONLINE_CONN_H__

#include "http/ws_session.h"
#include "include/iomanager.h"
#include <atomic>
#include <memory>
#include <stdint.h>
#include <string>

namespace chat {
namespace session {

struct OnlineConn {
    typedef std::shared_ptr<OnlineConn> ptr;

    uint64_t user_id = 0;
    std::string device_id;
    flz::http::WSSession::ptr session;
    flz::IOManager* io_manager = nullptr;
    int64_t login_at = 0;
    std::atomic<int64_t> last_active_ts;
    std::atomic<int64_t> bucket_refill_ts;
    std::atomic<int32_t> bucket_tokens;

    OnlineConn(uint64_t uid, const std::string& did, flz::http::WSSession::ptr s, flz::IOManager* iom,
               int64_t now_ms, int32_t rate_limit)
        : user_id(uid)
        , device_id(did)
        , session(s)
        , io_manager(iom)
        , login_at(now_ms)
        , last_active_ts(now_ms)
        , bucket_refill_ts(now_ms)
        , bucket_tokens(rate_limit) {
    }

    bool ConsumeToken(int64_t now_ms, int32_t rate_limit) {
        int64_t refill_ts = bucket_refill_ts.load();
        int32_t tokens = bucket_tokens.load();
        if(now_ms - refill_ts >= 1000) {
            bucket_refill_ts.store(now_ms);
            tokens = rate_limit;
            bucket_tokens.store(tokens);
        }
        if(tokens <= 0) {
            return false;
        }
        bucket_tokens.store(tokens - 1);
        return true;
    }
};

}
}

#endif
