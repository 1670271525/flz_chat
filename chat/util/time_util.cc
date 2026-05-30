#include "chat/util/time_util.h"
#include "include/util.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace chat {
namespace util {

int64_t NowMs() {
    return static_cast<int64_t>(flz::getCurrentMS());
}

std::string Iso8601FromMs(int64_t ts_ms, int timezone_offset_minutes) {
    int64_t ts_seconds = ts_ms / 1000;
    int offset_seconds = timezone_offset_minutes * 60;
    time_t shifted = static_cast<time_t>(ts_seconds + offset_seconds);
    struct tm tm_time;
    gmtime_r(&shifted, &tm_time);

    int tz_hour = timezone_offset_minutes / 60;
    int tz_min = timezone_offset_minutes % 60;
    if(tz_min < 0) {
        tz_min = -tz_min;
    }

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (tm_time.tm_year + 1900) << "-"
        << std::setw(2) << (tm_time.tm_mon + 1) << "-"
        << std::setw(2) << tm_time.tm_mday << "T"
        << std::setw(2) << tm_time.tm_hour << ":"
        << std::setw(2) << tm_time.tm_min << ":"
        << std::setw(2) << tm_time.tm_sec
        << (timezone_offset_minutes >= 0 ? "+" : "-")
        << std::setw(2) << std::abs(tz_hour) << ":"
        << std::setw(2) << tz_min;
    return oss.str();
}

std::string Iso8601Now() {
    return Iso8601FromMs(NowMs(), 8 * 60);
}

}
}
