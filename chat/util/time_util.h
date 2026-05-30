#pragma once
#ifndef __CHAT_UTIL_TIME_UTIL_H__
#define __CHAT_UTIL_TIME_UTIL_H__

#include <stdint.h>
#include <string>

namespace chat {
namespace util {

int64_t NowMs();
std::string Iso8601Now();
std::string Iso8601FromMs(int64_t ts_ms, int timezone_offset_minutes = 8 * 60);

}
}

#endif
