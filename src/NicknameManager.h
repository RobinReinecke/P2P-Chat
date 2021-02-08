#ifndef NICKNAMEMANAGER_H
#define NICKNAMEMANAGER_H

#include <string>
#include <array>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class NicknameManager {
public:
    NicknameManager();

    // methods
    std::string get(const std::string &hostname);
    bool add(const std::string &hostname, const std::string &nickname);
    bool remove(const std::string &hostname);
    bool rename(const std::string &oldNickname, const std::string &newNickname);
    std::string reverseLookup(const std::string &nickname);
    void loadJson(const json &json);
    json toJson();
    std::string generateRandomNickname();

    //statics
    static bool checkNickname(const std::string &nickname);
private:
    std::map<std::string, std::string> nicknames;
};

#endif
