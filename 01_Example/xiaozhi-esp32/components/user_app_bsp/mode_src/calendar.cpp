
#include <chrono>
#include "calendar.h"

std::chrono::system_clock::time_point toTimePoint(const Date& d) {
    std::tm tm = {};
    tm.tm_year = d.year - 1900; // years since 1900
    tm.tm_mon = d.month - 1;    // months since January [0-11]
    tm.tm_mday = d.day;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// Helper to convert time_point to Date
Date fromTimePoint(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm* tm = std::localtime(&t);
    return { tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday };
}

std::map<Date, std::string> EventCalendar::getNext7Days() {
    std::map<Date, std::string> result;

    auto now = std::chrono::system_clock::now();
    Date today = fromTimePoint(now);
    
    auto week_later_tp = now + std::chrono::hours(24 * 7);
    Date week_later = fromTimePoint(week_later_tp);

    for (const auto& [date, desc] : events) {
         std::cout << date.day << today.day << week_later.day << std::endl;
            
        if (true || (date >= today && date <= week_later)) {
           result[date] = desc;
        }
    }

    return result;
}

