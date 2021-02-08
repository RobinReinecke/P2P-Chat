#include <ctime>
#include "MessageManager.h"
#include "Enums.h"

MessageManager::MessageManager() = default;

/**
 * Get the json of a proposal.
 * @param id message id of the proposal
 * @return json of the proposal or nullptr if proposal is unknown
 */
json MessageManager::getProposal(const std::string &id) {
    pruneOldProposals();
    for (const auto &proposal : proposals) {
        if (proposal.data["id"] == id) return proposal.data;
    }
    return nullptr;
}

/**
 * Remove proposals that are older than 20 seconds.
 */
void MessageManager::pruneOldProposals() {
    proposals.erase(std::remove_if(proposals.begin(), proposals.end(),
                                   [](const Proposal &proposal) {
                                       return proposal.data["timestamp"] < std::time(nullptr) - 20;
                                   }), proposals.end());
}

/**
 * Add a json as proposal.
 * @param json which contains a message id
 * @return false if something went wrong
 */
bool MessageManager::addProposal(const json &json) {
    pruneOldProposals();
    if (json == nullptr || json.value("id", "").empty()) return false;
    if (getProposal((std::string) json["id"]) != nullptr) return false;

    proposals.push_back({json, std::set<std::string>()});
    return true;
}

/**
 * Remove a proposal.
 * @param id of the proposal
 */
void MessageManager::removeProposal(const std::string &id) {
    pruneOldProposals();
    proposals.erase(std::remove_if(proposals.begin(), proposals.end(),
                                   [&id](const Proposal &proposal) {
                                       return proposal.data["id"] == id;
                                   }), proposals.end());
}

/**
 * Add a confirmation to a proposal.
 * @param id message id of the proposal
 * @return new count of confirmations
 */
int MessageManager::addProposalConfirmation(const std::string &id, const std::string &origin) {
    pruneOldProposals();
    for (auto &proposal : proposals) {
        if (proposal.data["id"] == id) {
            // prevent confirmations from the same peer received multiple times counted multiple times
            proposal.confirmations.insert(origin);
            return proposal.confirmations.size();
        }
    }
    return 0;
}

/**
 * Check if the message id was already received.
 * @param id string with hostname and id
 * @return true = message already received
 */
bool MessageManager::checkReceivedStatus(const std::string &id) {
    pruneOldProposals();
    auto split = id.find_last_of('-');
    auto hostname = id.substr(0, split);
    int number = std::stoi(id.substr(split + 1));

    auto iterator = messageIds.find(hostname);
    if (iterator == messageIds.end()) {
        // hostname is unknown
        messageIds.emplace(hostname, number);
        return false;
    }

    if (iterator->second < number) {
        // known id is smaller, thus the passed id is newer
        iterator->second = number;
        return false;
    }
    // passed id is smaller or equal and thus already known
    return true;
}

/**
 * Remove the counter of message ids for a peer. Should be called when a peer disconnects.
 * @param hostname disconnected peer
 */
void MessageManager::removeMessageId(const std::string &hostname) {
    pruneOldProposals();
    messageIds.erase(hostname);
}

/**
 * Check if the passed proposal is blocked by an existing proposal.
 * @param message
 * @return true = passed proposal is blocked
 */
bool MessageManager::checkProposalBlocked(const json &message) {
    for (const auto &proposal :proposals) {
        switch (static_cast<Type>(message["type"])) {
            case Type::NICK: {
                // only process same types
                if (proposal.data["type"] != message["type"]) continue;
                // if they try to pick the same nickname
                if (((std::string) message["payload"]["target"]) == ((std::string) proposal.data["payload"]["target"]))
                    return true;
                break;
            }
            case Type::JOIN: {
                // only process create and leave
                // create: Group should be created before leave
                // leave: It could be possible that everyone leaves
                auto type = static_cast<Type>(proposal.data["type"]);
                if (type != Type::CREATE && type != Type::LEAVE) continue;
                // if its the same group
                if (((std::string) message["payload"]["target"]) == ((std::string) proposal.data["payload"]["target"]))
                    return true;
                break;
            }
            case Type::CREATE: {
                // only process same types
                if (proposal.data["type"] != message["type"]) continue;
                // if they try to create the same group
                if (((std::string) message["payload"]["target"]) == ((std::string) proposal.data["payload"]["target"]))
                    return true;
                break;
            }
            case Type::LEAVE: {
                // dont leave a group if another one tries to join it
                if (static_cast<Type>(proposal.data["type"]) != Type::JOIN) continue;
                // if they try to join the group
                if (((std::string) message["payload"]["target"]) == ((std::string) proposal.data["payload"]["target"]))
                    return true;
                break;
            }
            default:
                return false;
        }
    }
    return false;
}
