#include "chat/mq/envelope.h"
#include "chat/util/time_util.h"
#include "chat/util/uuid.h"
#include "include/json_util.h"

namespace chat {
namespace mq {

bool EnvelopeToJson(const Envelope& env, Json::Value& out) {
    out["msgId"] = env.msg_id;
    out["version"] = env.version;
    out["occurredAt"] = env.occurred_at;
    out["source"] = env.source;
    out["type"] = env.type;
    out["payload"] = env.payload;
    return true;
}

std::string EnvelopeToString(const Envelope& env) {
    Json::Value json;
    EnvelopeToJson(env, json);
    return flz::JsonUtil::ToString(json);
}

bool EnvelopeFromJson(const Json::Value& in, Envelope& out, std::string& err) {
    if(!in.isObject()) {
        err = "envelope must be object";
        return false;
    }
    out.msg_id = in.get("msgId", "").asString();
    out.version = in.get("version", 0).asInt();
    out.occurred_at = in.get("occurredAt", "").asString();
    out.source = in.get("source", "").asString();
    out.type = in.get("type", "").asString();
    out.payload = in["payload"];

    if(out.msg_id.empty() || out.type.empty() || out.version <= 0) {
        err = "envelope required fields missing";
        return false;
    }
    if(!out.payload.isObject() && !out.payload.isArray()) {
        out.payload = Json::Value(Json::objectValue);
    }
    return true;
}

bool EnvelopeFromString(const std::string& in, Envelope& out, std::string& err) {
    Json::Value json;
    if(!flz::JsonUtil::FromString(json, in)) {
        err = "envelope json parse failed";
        return false;
    }
    return EnvelopeFromJson(json, out, err);
}

Envelope BuildEnvelope(const std::string& source, const std::string& type, const Json::Value& payload) {
    Envelope env;
    env.msg_id = util::Uuid::V4();
    env.version = 1;
    env.occurred_at = util::Iso8601Now();
    env.source = source;
    env.type = type;
    env.payload = payload;
    return env;
}

}
}
