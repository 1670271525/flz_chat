#pragma once
#ifndef __CHAT_MQ_ENVELOPE_H__
#define __CHAT_MQ_ENVELOPE_H__

#include <json/json.h>
#include <string>

namespace chat {
namespace mq {

struct Envelope {
    std::string msg_id;
    int version = 1;
    std::string occurred_at;
    std::string source;
    std::string type;
    Json::Value payload = Json::Value(Json::objectValue);
};

bool EnvelopeToJson(const Envelope& env, Json::Value& out);
std::string EnvelopeToString(const Envelope& env);
bool EnvelopeFromJson(const Json::Value& in, Envelope& out, std::string& err);
bool EnvelopeFromString(const std::string& in, Envelope& out, std::string& err);
Envelope BuildEnvelope(const std::string& source, const std::string& type, const Json::Value& payload);

}
}

#endif
