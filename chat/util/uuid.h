#pragma once
#ifndef __CHAT_UTIL_UUID_H__
#define __CHAT_UTIL_UUID_H__

#include <string>

namespace chat {
namespace util {

class Uuid {
public:
    static std::string V4();
};

}
}

#endif
