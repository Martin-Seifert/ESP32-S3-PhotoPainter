
#include <chrono>
#include "calendar.h"

std::chrono::system_clock::time_point toTimePoint(const Date& d) {
    std::tm tm = {};
    tm.tm_year = d.year - 1900; // years since 1900
    tm.tm_mon = d.month - 1;    // months since January [0-11]
    tm.tm_mday = d.day;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}