
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

std::map<Date, std::string> EventCalendar::getNextDays(int days) {
    std::map<Date, std::string> result;

    auto now = std::chrono::system_clock::now();
    Date today = fromTimePoint(now);
    
    auto week_later_tp = now + std::chrono::hours(24 * days);
    Date week_later = fromTimePoint(week_later_tp);

    for (const auto& [date, desc] : events) {
         std::cout << date.day << today.day << week_later.day << std::endl;
            
        if ((date >= today && date <= week_later)) {
           result[date] = desc;
        }
    }

    return result;
}


/**
 * @brief Checks if the current system time is higher (later) than a specified target date and time.
 * 
 * @param targetDateTime The target date and time in ISO format: "YYYY-MM-DDTHH:MM:SS" (seconds are optional).
 * @return bool True if current time is strictly later than target time, false otherwise.
 */
bool isCurrentTimeHigherThan(const std::string targetDateTime) {
    // 1. Get current time_t timestamp (seconds since Unix Epoch)
    time_t now = time(0);
    
    // 2. Prepare a struct tm for the target time parsing
    struct tm targetTm = {0}; // Initialize to all zeros

    // 3. Parse the input string into the struct tm using std::get_time (C++11/14/17)
    // The format string "%Y-%m-%d %H:%M:%S" works for the full format.
    // We try to parse the string using a stringstream for more robust parsing.
    std::istringstream ss(targetDateTime);
    
    // Check if the format includes seconds or just minutes
    const char* formatStr = "%Y-%m-%dT%H:%M:%S";
    if (targetDateTime.find_last_of(':') == targetDateTime.length() - 3) {
        // Assume format "YYYY-MM-DD HH:MM"
        formatStr = "%Y-%m-%dT%H:%M";
    }

    ss >> std::get_time(&targetTm, formatStr);

    if (ss.fail()) {
        std::cerr << "Error: Failed to parse target time string. Use format 'YYYY-MM-DD HH:MM:SS' or 'YYYY-MM-DD HH:MM'." << std::endl;
        return false; // Or handle error as needed
    }

    // 4. Convert the parsed struct tm into a time_t timestamp
    // mktime() adjusts fields and handles time zones/daylight savings based on local settings.
    time_t targetTimestamp = std::mktime(&targetTm);

    if (targetTimestamp == -1) {
        std::cerr << "Error: Invalid target date/time provided." << std::endl;
        return false;
    }
    
    // Optional: Print timestamps for debugging
    // std::cout << "Current Epoch Time: " << now << std::endl;
    // std::cout << "Target Epoch Time:  " << targetTimestamp << std::endl;

    // 5. Compare the two time_t values
    return now > targetTimestamp;
}