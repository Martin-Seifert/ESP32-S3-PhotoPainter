#ifndef CALENDAR_H
#define CALENDAR_H


#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <sstream>
#include <iomanip>

bool isCurrentTimeHigherThan(const std::string targetDateTime);

struct Date {
    int year, month, day;

    void FromYYYY_MM_DD(std::string str){

        std::stringstream ss(str);
        char dash;    
        ss >> year >> dash >> month >> dash >> day;
    }

    std::string ToYYYY_MM_DD() {
        std::ostringstream ss;

        // Format the year, month, and day into the YYYY-MM-DD format
        ss << year << "-" 
        << std::setw(2) << std::setfill('0') << month << "-"  // Ensure 2-digit month
        << std::setw(2) << std::setfill('0') << day;          // Ensure 2-digit day

        return ss.str();
    }

    std::string ToDD_MM_YYYY() {
        std::ostringstream ss;

        // Format the year, month, and day into the YYYY-MM-DD format
        ss << std::setw(2) << std::setfill('0') << day << "." 
        << std::setw(2) << std::setfill('0') << month << "."  // Ensure 2-digit month
        << year;          // Ensure 2-digit day

        return ss.str();
    }

    std::string ToDD_MM() {
        std::ostringstream ss;

        // Format the year, month, and day into the YYYY-MM-DD format
        ss << std::setw(2) << std::setfill('0') << day << "." 
        << std::setw(2) << std::setfill('0') << month;  // Ensure 2-digit month

        return ss.str();
    }

    // Comparison operator for sorting
    bool operator<(const Date& other) const {
        return std::tie(year, month, day) < std::tie(other.year, other.month, other.day);
    }

    bool operator==(const Date& other) const {
        return std::tie(year, month, day) == std::tie(other.year, other.month, other.day);
    }

    // Less than or equal
    bool operator<=(const Date& other) const {
        return *this < other || *this == other;
    }

    // Greater than
    bool operator>(const Date& other) const {
        return other < *this;
    }

    // Greater than or equal
    bool operator>=(const Date& other) const {
        return *this > other || *this == other;
    }
};

class EventCalendar {
private:
    std::map<Date, std::string> events;

public:
    // Add a date + event
    void addEvent(const Date& date, const std::string& description) {
        events[date] = description;
    }

    // Get all events in the next 7 days
    std::map<Date, std::string> getNextDays(int days);
    
    // Print all events (for testing)
    void printEvents(const std::map<Date, std::string>& evts) {
        for (const auto& [date, desc] : evts) {
            std::cout << date.year << "-" << date.month << "-" << date.day << ": " << desc << "\n";
        }
    }
};

#endif