#include "IpManager.h"

IpManager::IpManager() = default;

/**
 * Get ip for a passed hostname.
 * @param hostname
 * @return ip or empty string if unknown
 */
std::string IpManager::get(const std::string &hostname) {
    auto nickname = ips.find(hostname);

    if (nickname == ips.end()) return "";
    return nickname->second;
}

/**
 * Get hostname for a passed ip.
 * @param ip
 * @return hostname or empty string if unknown
 */
std::string IpManager::reverseLookup(const std::string &ip) {
    for (const auto &pair : ips) {
        if (pair.second == ip) {
            return pair.first;
        }
    }
    return "";
}

/**
 * Add a new pair of hostname and ip.
 * @param hostname
 * @param ip
 * @return false if something went wrong
 */
bool IpManager::add(const std::string &hostname, const std::string &ip) {
    // check if nickname is already taken
    if (!reverseLookup(ip).empty()) return false;
    return ips.emplace(hostname, ip).second;
}

/**
 * Remove the hostname.
 * @param hostname
 * @return false if hostname is unknown
 */
bool IpManager::remove(const std::string &hostname) {
    return ips.erase(hostname) > 0;
}

/**
 * Load data from json.
 * @param json
 */
void IpManager::loadJson(const json &json) {
    for (const auto &element: json.items()) {
        add(element.value()[0], element.value()[1]);
    }
}

/**
 * Convert current state to json.
 * @return json of data
 */
json IpManager::toJson() {
    json j;
    for (const auto &element: ips) {
        j.push_back({element.first, element.second});
    }
    return j;
}