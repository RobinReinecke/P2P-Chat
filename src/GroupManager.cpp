#include <algorithm>
#include "GroupManager.h"

GroupManager::GroupManager() = default;

/**
 * Get a group by its name.
 * @param name of the group
 * @return Pointer to group or nullptr if not found
 */
Group *GroupManager::get(const std::string &name) {
    auto group = std::find_if(groups.begin(), groups.end(),
                              [&](const Group &group) { return group.getName() == name; });
    if (group != groups.end()) return &(*group);
    return nullptr;
}

/**
 * Create a group.
 * @param name of new group
 * @param admin of new group
 * @return Pointer to created group or nullptr name already exists
 */
Group *GroupManager::create(const std::string &name, const std::string &admin) {
    // check if group exist
    if (get(name) != nullptr) {
        return nullptr;
    }
    groups.emplace_back(name, admin);

    Group *group = get(name);
    group->addMember(admin);

    return group;
}

/**
 * Remove a hostname from all groups
 * @param hostname
 * @return set of groupnames the hostname was member of
 */
std::set<std::string> GroupManager::removeFromAllGroups(const std::string &hostname) {
    std::set<std::string> leftGroups;
    for (auto &group: groups) {
        if (group.isMember(hostname)) {
            leftGroups.insert(group.getName());
            group.removeMember(hostname);
        }
    }
    return leftGroups;
}

/**
 * Remove all empty Groups
 * @return set of names of removed groups
 */
std::set<std::string> GroupManager::removeEmptyGroups() {
    std::set<std::string> emptyGroups;
    for (const auto &group: groups) {
        if (group.isEmpty()) emptyGroups.insert(group.getName());
    }
    groups.erase(remove_if(groups.begin(), groups.end(), [](const Group &group) { return group.isEmpty(); }),
                 groups.end());
    return emptyGroups;
}

/**
 * Load data from json.
 * @param json
 */
void GroupManager::loadJson(const json &json) {
    for (const auto &element: json.items()) {
        auto data = element.value()[1];
        auto group = create(element.value()[0], data["admin"]);
        group->setTopic(data["topic"]);
        for (const auto &member : data["members"].items()) {
            group->addMember(member.value());
        }
    }
}

/**
 * Convert current state to json.
 * @return json of data
 */
json GroupManager::toJson() const {
    json j;
    for (auto &element: groups) {
        j.push_back(element.toJson());
    }
    return j;
}

/**
 * Create a string of the existing groups.
 * @return string comma separated
 */
std::string GroupManager::toString() const {
    std::string listedGroups;
    for (const auto &group : groups) {
        listedGroups += group.getName() + ", ";
    }
    return listedGroups;
}
