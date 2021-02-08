#include <regex>
#include <unistd.h>
#include "NicknameManager.h"

NicknameManager::NicknameManager() = default;

/**
 * Get nickname for a passed hostname.
 * @param hostname
 * @return nickname or empty string if unknown
 */
std::string NicknameManager::get(const std::string &hostname) {
    auto nickname = nicknames.find(hostname);

    if (nickname == nicknames.end()) return "";
    return nickname->second;
}

/**
 * Get hostname for a passed nickname.
 * @param nickname
 * @return hostname or empty string if unknown
 */
std::string NicknameManager::reverseLookup(const std::string &nickname) {
    for (const auto &pair : nicknames) {
        if (pair.second == nickname) {
            return pair.first;
        }
    }
    return "";
}

/**
 * Add a new pair of hostname and nickname
 * @param hostname
 * @param nickname
 * @return false if something went wrong
 */
bool NicknameManager::add(const std::string &hostname, const std::string &nickname) {
    // check if nickname is already taken
    if (!reverseLookup(nickname).empty()) return false;
    return nicknames.emplace(hostname, nickname).second;
}

/**
 * Remove the hostname.
 * @param hostname
 * @return false if hostname is unknown
 */
bool NicknameManager::remove(const std::string &hostname) {
    return nicknames.erase(hostname) > 0;
}

/**
 * Rename a hostname.
 * @param hostname
 * @param newNickname
 * @return false if something went wrong
 */
bool NicknameManager::rename(const std::string &hostname, const std::string &newNickname) {
    if (get(hostname).empty()) return false;

    auto pair = nicknames.find(hostname);
    pair->second = newNickname;
    return true;
}

/**
 * Load data from json.
 * @param json
 */
void NicknameManager::loadJson(const json &json) {
    for (const auto &element: json.items()) {
        add(element.value()[0], element.value()[1]);
    }
}

/**
 * Convert current state to json.
 * @return json of data
 */
json NicknameManager::toJson() {
    json j;
    for (const auto &element: nicknames) {
        j.push_back({element.first, element.second});
    }
    return j;
}

/**
 * Check if a string is a valid nickname.
 * 9 chars longs and must consist of letters or numbers
 * @param nickname
 * @return boolean
 */
bool NicknameManager::checkNickname(const std::string &nickname) {
    std::regex nickRegex;
    nickRegex.assign(R"(^[A-Za-z0-9]{1,9}$)");
    return regex_match(nickname, nickRegex);
}

/**
 * Generate a random nickname that is not taken.
 * @return nickname
 */
std::string NicknameManager::generateRandomNickname() {
    const int len = 9;
    std::string tmp_s;
    static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    do {
        tmp_s = std::string();
        srand((unsigned) time(nullptr) * getpid());

        for (int i = 0; i < len; ++i)
            tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    } while (!reverseLookup(tmp_s).empty());

    return tmp_s;
}