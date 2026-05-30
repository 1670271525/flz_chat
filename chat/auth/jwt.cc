#include "chat/auth/jwt.h"
#include "chat/config/chat_config.h"
#include "chat/util/base64url.h"
#include "include/json_util.h"
#include "include/util.h"
#include <cstdlib>
#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <sstream>
#include <vector>

namespace chat {
namespace auth {

namespace {

static std::vector<std::string> Split(const std::string& s, char ch) {
    std::vector<std::string> parts;
    std::string current;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == ch) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(s[i]);
        }
    }
    parts.push_back(current);
    return parts;
}

static bool ParseUInt64(const std::string& in, uint64_t& out) {
    if(in.empty()) {
        return false;
    }
    char* end = nullptr;
    unsigned long long v = strtoull(in.c_str(), &end, 10);
    if(end == nullptr || *end != '\0') {
        return false;
    }
    out = static_cast<uint64_t>(v);
    return true;
}

}

bool JwtVerifier::Verify(const std::string& token, JwtClaims& out, std::string& err) {
    out = JwtClaims();
    const config::JwtConfig& cfg = config::ChatConfig::GetInstance().jwt();

    std::vector<std::string> parts = Split(token, '.');
    if(parts.size() != 3 || parts[0].empty() || parts[1].empty() || parts[2].empty()) {
        err = "token format invalid";
        return false;
    }

    std::string header_json;
    std::string payload_json;
    if(!util::Base64Url::Decode(parts[0], header_json) || !util::Base64Url::Decode(parts[1], payload_json)) {
        err = "token base64 decode failed";
        return false;
    }

    Json::Value header;
    Json::Value payload;
    if(!flz::JsonUtil::FromString(header, header_json) || !header.isObject()) {
        err = "token header invalid";
        return false;
    }
    if(!flz::JsonUtil::FromString(payload, payload_json) || !payload.isObject()) {
        err = "token payload invalid";
        return false;
    }

    const std::string alg = header.get("alg", "").asString();
    if(alg != "HS256") {
        err = "token alg invalid";
        return false;
    }

    const std::string signing_input = parts[0] + "." + parts[1];
    unsigned int digest_len = 0;
    unsigned char digest[EVP_MAX_MD_SIZE];
    if(HMAC(EVP_sha256(),
            cfg.secret.data(), static_cast<int>(cfg.secret.size()),
            reinterpret_cast<const unsigned char*>(signing_input.data()),
            signing_input.size(), digest, &digest_len) == nullptr) {
        err = "hmac failed";
        return false;
    }
    std::string expected_sig;
    util::Base64Url::Encode(reinterpret_cast<const char*>(digest), digest_len, expected_sig);
    if(expected_sig.size() != parts[2].size()
            || CRYPTO_memcmp(expected_sig.data(), parts[2].data(), expected_sig.size()) != 0) {
        err = "token signature invalid";
        return false;
    }

    out.iss = payload.get("iss", "").asString();
    if(out.iss != cfg.issuer) {
        err = "token issuer mismatch";
        return false;
    }

    const int64_t now = static_cast<int64_t>(time(nullptr));
    out.exp = payload.get("exp", 0).asInt64();
    out.iat = payload.get("iat", 0).asInt64();
    if(out.exp <= 0 || now > out.exp + cfg.clock_skew_seconds) {
        err = "token expired";
        return false;
    }
    if(out.iat > 0 && now + cfg.clock_skew_seconds < out.iat) {
        err = "token iat invalid";
        return false;
    }

    std::string sub = payload.get("sub", "").asString();
    if(sub.empty()) {
        if(payload["sub"].isUInt64() || payload["sub"].isInt64()) {
            sub = payload["sub"].asString();
        }
    }
    if(!ParseUInt64(sub, out.user_id) || out.user_id == 0) {
        err = "token sub invalid";
        return false;
    }
    out.device_id = payload.get("did", "").asString();
    out.raw_payload = payload;
    return true;
}

}
}
