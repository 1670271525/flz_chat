#pragma once
#ifndef __CHAT_AUTH_JWT_CLAIMS_H__
#define __CHAT_AUTH_JWT_CLAIMS_H__

#include <stdint.h>
#include <string>
#include <json/json.h>

namespace chat {
namespace auth {

struct JwtClaims {
    uint64_t user_id = 0;
    std::string device_id;
    int64_t iat = 0;
    int64_t exp = 0;
    std::string iss;
    Json::Value raw_payload;
};

}
}

#endif
