#include "Group.h"

Group::Group(const std::string &name, const std::string &admin) : name(name), admin(admin) {
}

/**
* Remove the passed peer from the client vector
 * @param hostname of the peer
*/
void Group::removeMember(const std::string &hostname) {
    members.erase(hostname);
    // check if new admin is needed
    if (!members.empty() && hostname == admin) {
        std::vector<std::string> membersCopy(members.begin(), members.end());
        // take the alphabetical first member
        std::sort(membersCopy.begin(), membersCopy.end());
        admin = membersCopy.at(0);
        changedAdmin = true;
    }
    else changedAdmin = false;
}

/**
* Add Peer to a group
 * @param hostname of the peer
*/
void Group::addMember(const std::string &hostname) {
    members.insert(hostname);
}

/**
 * Get member status of passed peer.
 * @param hostname of peer
 * @return true: is member
 */
bool Group::isMember(const std::string &hostname) const {
    return members.count(hostname);
}

/**
 * Get empty status of the group.
 * @return true: group is empty
 */
bool Group::isEmpty() const {
    return members.empty();
}

/**
 * Convert to json.
 * @return json of this group
 */
json Group::toJson() const {
    return {
            name, {
                    {"admin", admin},
                    {"topic", topic},
                    {"members", members}
            }
    };
}
