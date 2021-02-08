#ifndef HELPER_H
#define HELPER_H

#include <algorithm>
#include <cctype>
#include <locale>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * Trim from start.
 * @param s String to trim
 */
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

/**
 * Trim from end.
 * @param s String to trim
 */
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(),
            s.end());
}

/**
 * Trim from both ends
 * @param s String to trim
 */
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

/**
 * Trim from start.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

/**
 * Trim from end.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

/**
 * Trim from both ends.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

/**
 * Try to parse a string into a json.
 * @param buffer json string
 * @return nullptr if parsing failed
 */
static inline json tryParse(const std::string &buffer) {
    try {
        return json::parse(buffer);
    }
    catch (nlohmann::detail::parse_error &ex) {
    }
    return nullptr;
}

#endif
