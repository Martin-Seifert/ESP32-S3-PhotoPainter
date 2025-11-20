#ifndef CALENDAR_H
#define CALENDAR_H


#include <iostream>
#include <map>
#include <string>
#include <tuple>

std::map<Date, std::string> events;


struct Date {
    int year, month, day;

    // Comparison operator for sorting
    bool operator<(const Date& other) const {
        return std::tie(year, month, day) < std::tie(other.year, other.month, other.day);
    }
};



