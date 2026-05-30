#pragma once
#ifndef __CHAT_PROTOCOL_H__
#define __CHAT_PROTOCOL_H__

#include "http/ws_session.h"
#include <json/json.h>
#include <string>

namespace chat {

struct ClientFrame {
    std::string type;
    bool has_seq = false;
    int64_t seq = 0;
    Json::Value data = Json::Value(Json::objectValue);
};

class Protocol {
public:
    static bool ParseClientFrame(const std::string& raw, ClientFrame& out, std::string& err);
    static Json::Value BuildFrame(const std::string& type, const Json::Value& data, bool has_seq = false, int64_t seq = 0);
    static int32_t SendFrame(flz::http::WSSession::ptr session, const std::string& type,
                             const Json::Value& data, bool has_seq = false, int64_t seq = 0);
    static int32_t SendError(flz::http::WSSession::ptr session, int code, const std::string& msg,
                             bool has_seq = false, int64_t seq = 0);
};

}





#endif
