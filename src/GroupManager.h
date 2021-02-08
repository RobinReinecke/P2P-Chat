#ifndef GROUPMANAGER_H
#define GROUPMANAGER_H

#include <vector>
#include "Group.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class GroupManager {
public:
    GroupManager();

    Group *get(const std::string &name);
    Group *create(const std::string &name, const std::string &admin);
    std::set<std::string> removeFromAllGroups(const std::string &hostname);
    std::set<std::string> removeEmptyGroups();
    void loadJson(const json &json);
    json toJson() const;
    std::string toString() const;

private:
    std::vector<Group> groups;
};

#endif
