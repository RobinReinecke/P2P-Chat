#ifndef IPMANAGER_H
#define IPMANAGER_H

#include <string>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class IpManager {
public:
    IpManager();
    std::string get(const std::string &hostname);
    bool add(const std::string &hostname, const std::string &ip);
    bool remove(const std::string &hostname);
    std::string reverseLookup(const std::string &ip);
    void loadJson(const json &json);
    json toJson();

private:
    std::map<std::string, std::string> ips;
};

#endif
