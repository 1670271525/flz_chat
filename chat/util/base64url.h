#pragma once
#ifndef __CHAT_UTIL_BASE64URL_H__
#define __CHAT_UTIL_BASE64URL_H__

#include <stddef.h>
#include <string>

namespace chat {
namespace util {

class Base64Url {
public:
    static bool Encode(const std::string& in, std::string& out);
    static bool Encode(const char* data, size_t len, std::string& out);
    static bool Decode(const std::string& in, std::string& out);
};

}
}

#endif
