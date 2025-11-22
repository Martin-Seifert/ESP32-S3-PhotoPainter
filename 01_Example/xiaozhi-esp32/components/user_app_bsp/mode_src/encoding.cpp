#include <iostream>
#include <string>
#include <stdexcept>
#include <locale>
#include <codecvt>

std::string utf8_to_latin1(const std::string& utf8_str) {
    std::string latin1_str;

    // Iterate through each byte in the UTF-8 string
    size_t i = 0;
    while (i < utf8_str.size()) {
        unsigned char c = utf8_str[i];

        if (c <= 0x7F) {  // ASCII character (fits in Latin-1)
            latin1_str.push_back(c);
            ++i;
        } else if (c >= 0xC0 && c <= 0xDF) {  // Two-byte UTF-8 sequence
            if (i + 1 < utf8_str.size()) {
                unsigned char second_byte = utf8_str[i + 1];
                unsigned int code_point = ((c & 0x1F) << 6) | (second_byte & 0x3F);

                // Check if the code point fits in Latin-1
                if (code_point <= 0xFF) {
                    latin1_str.push_back(static_cast<char>(code_point));
                } else {
                    latin1_str.push_back('?');  // Invalid Latin-1 char, replace with ?
                }

                i += 2;
            } else {
                latin1_str.push_back('?');
                ++i;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {  // Three-byte UTF-8 sequence
            if (i + 2 < utf8_str.size()) {
                unsigned char second_byte = utf8_str[i + 1];
                unsigned char third_byte = utf8_str[i + 2];
                unsigned int code_point = ((c & 0x0F) << 12) | ((second_byte & 0x3F) << 6) | (third_byte & 0x3F);

                // Check if the code point fits in Latin-1
                if (code_point <= 0xFF) {
                    latin1_str.push_back(static_cast<char>(code_point));
                } else {
                    latin1_str.push_back('?');  // Invalid Latin-1 char, replace with ?
                }

                i += 3;
            } else {
                latin1_str.push_back('?');
                ++i;
            }
        } else if (c >= 0xF0 && c <= 0xF7) {  // Four-byte UTF-8 sequence
            if (i + 3 < utf8_str.size()) {
                unsigned char second_byte = utf8_str[i + 1];
                unsigned char third_byte = utf8_str[i + 2];
                unsigned char fourth_byte = utf8_str[i + 3];
                unsigned int code_point = ((c & 0x07) << 18) | ((second_byte & 0x3F) << 12) | ((third_byte & 0x3F) << 6) | (fourth_byte & 0x3F);

                // Check if the code point fits in Latin-1
                if (code_point <= 0xFF) {
                    latin1_str.push_back(static_cast<char>(code_point));
                } else {
                    latin1_str.push_back('?');  // Invalid Latin-1 char, replace with ?
                }

                i += 4;
            } else {
                latin1_str.push_back('?');
                ++i;
            }
        } else {
            latin1_str.push_back('?');  // Invalid byte, replace with ?
            ++i;
        }
    }

    return latin1_str;
}