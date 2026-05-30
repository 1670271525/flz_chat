#include "protocol.h"
#include "include/json_util.h"
#include "include/log.h"

namespace chat {

static flz::Logger::ptr g_logger = FLZ_LOG_NAME("system");

bool Protocol::ParseClientFrame(const std::string& raw, ClientFrame& out, std::string& err) {
    Json::Value json;
    if(!flz::JsonUtil::FromString(json, raw)) {
        err = "bad json";
        return false;
    }
    if(!json.isObject()) {
        err = "frame must be object";
        return false;
    }
    out.type = json.get("type", "").asString();
    if(out.type.empty()) {
        err = "type required";
        return false;
    }
    if(json.isMember("seq")) {
        if(!json["seq"].isInt64() && !json["seq"].isUInt64() && !json["seq"].isInt()) {
            err = "seq must be integer";
            return false;
        }
        out.has_seq = true;
        out.seq = json["seq"].asInt64();
    } else {
        out.has_seq = false;
        out.seq = 0;
    }
    if(json.isMember("data")) {
        out.data = json["data"];
    } else {
        out.data = Json::Value(Json::objectValue);
    }
    return true;
}

Json::Value Protocol::BuildFrame(const std::string& type, const Json::Value& data, bool has_seq, int64_t seq) {
    Json::Value out;
    out["type"] = type;
    if(has_seq) {
        out["seq"] = Json::Int64(seq);
    }
    out["data"] = data;
    return out;
}

int32_t Protocol::SendFrame(flz::http::WSSession::ptr session, const std::string& type,
                            const Json::Value& data, bool has_seq, int64_t seq) {
    if(!session) {
        return -1;
    }
    Json::Value frame = BuildFrame(type, data, has_seq, seq);
    const std::string text = flz::JsonUtil::ToString(frame);
    int32_t rt = session->sendMessage(text, flz::http::WSFrameHead::TEXT_FRAME, true);
    if(rt <= 0) {
        FLZ_LOG_WARN(g_logger) << "send frame failed type=" << type;
        return -1;
    }
    return 0;
}

int32_t Protocol::SendError(flz::http::WSSession::ptr session, int code, const std::string& msg,
                            bool has_seq, int64_t seq) {
    Json::Value data;
    data["code"] = code;
    data["msg"] = msg;
    return SendFrame(session, "error", data, has_seq, seq);
}

}
