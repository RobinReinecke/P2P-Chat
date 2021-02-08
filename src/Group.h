#ifndef GROUP_H
#define GROUP_H

#include <string>
#include <set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Group {
public:
    Group(const std::string &name, const std::string &admin);

    // methods
    void removeMember(const std::string &hostname);
    void addMember(const std::string &hostname);
    bool isMember(const std::string &hostname) const;
    bool isEmpty() const;
    json toJson() const;

    // getter & setter
    const std::string &getName() const { return name; }
    const std::string &getTopic() const { return topic; }
    void setTopic(const std::string &topic) { Group::topic = topic; }
    const std::string &getAdmin() const { return admin; }
    const std::set<std::string> &getMembers() const { return members; }
    bool hasChangedAdmin() const { return changedAdmin; }

private:
    std::string name;
    std::string topic;
    std::string admin;
    std::set<std::string> members;
    bool changedAdmin; // indicates if the last "removeMember" changed the admin
};

#endif
