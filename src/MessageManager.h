#ifndef PROPOSALMANAGER_H
#define PROPOSALMANAGER_H

#include <nlohmann/json.hpp>
#include <set>

using json = nlohmann::json;

class MessageManager {
public:
    struct Proposal {
        json data;
        std::set<std::string> confirmations;
    };

    MessageManager();

    // methods
    json getProposal(const std::string &id);
    bool addProposal(const json &json);
    void removeProposal(const std::string &id);
    bool checkReceivedStatus(const std::string &id);
    void removeMessageId(const std::string &hostname);
    int addProposalConfirmation(const std::string &id, const std::string &origin);
    bool checkProposalBlocked(const json &message);

private:
    // fields
    std::vector<Proposal> proposals;
    std::map<std::string, int> messageIds; // map of currently highest known message ids

    // methods
    void pruneOldProposals();
};

#endif
