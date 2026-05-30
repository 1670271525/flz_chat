#pragma once
#ifndef __CHAT_AUTH_JWT_H__
#define __CHAT_AUTH_JWT_H__

#include "chat/auth/jwt_claims.h"
#include <string>

namespace chat {
namespace auth {

class JwtVerifier {
public:
    static bool Verify(const std::string& token, JwtClaims& out, std::string& err);
};

}
}

#endif
