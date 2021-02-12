#ifndef CLIENT_H
#define CLIENT_H

#include <queue>
#include <nlohmann/json.hpp>
#include "Enums.h"
#include "NetworkManager.h"
#include "Logger.h"
#include "Topology.h"
#include "GroupManager.h"
#include "NicknameManager.h"
#include "MessageManager.h"
#include "IpManager.h"
#include "CryptoManager.h"

using json = nlohmann::json;

#define MULTICAST_PORT 5432
#define PEER_PORT 6543

class Client {

public:
    explicit Client(bool debug, uint16_t multicastPort = MULTICAST_PORT, uint16_t peerPort = PEER_PORT,
                    const std::string &nickname = "");

    // methods
    void pushCommand(const std::string &command);
    bool hasOutput();
    std::string popOutputMessage();
    void start();

private:
    // fields
    NetworkManager network;
    Logger &logger;
    std::queue<std::string> inputCommandQueue; // Commands to be executed
    Topology topology;  // network structure
    GroupManager groups;
    NicknameManager nicknames;
    MessageManager messages;
    IpManager ips;
    std::string nickname;

    // methods
    void processInput();
    void processCommand(Type type, std::string &target, const std::string &text);
    std::set<std::string> getNextHops(const std::string &recipient, bool checkHostname, bool checkGroupname);
    void receiveNetworkData();
    void processMulticastMessage(json &message);
    void processPeerMessage(json &message);
    void processProposal(json &message);
    void executeProposal(const std::string &id);
    bool isRecipient(const std::string &hostname, const std::string &recipient);
    void handleNetworkFracture();
    void handleNetworkUnderconnected();
    void handlePeerCommandJoin(const std::string &hostname, const std::string &groupname);
    void handlePeerCommandCreate(const std::string &hostname, const std::string &groupname);
    void handlePeerCommandLeave(const std::string &hostname, const std::string &groupname);
    void handlePeerCommandNick(const std::string &hostname, const std::string &nick);
    void handleInputCommandList();
    void handleInputCommandGetTopic(const std::string &groupname);
    void handlePeerCommandSetTopic(const std::string &hostname, const std::string &groupname, const std::string &text);
    void handlePeerCommandMsg(const std::string &hostname, const std::string &recipient, const std::string &text);
    void handleInputCommandQuit();
    void handleInputCommandHelp();
    void handleInputCommandGetMembers(const std::string &groupname);
    void handleInputCommandGetPublicKey(const std::string &targetNickname);
    void handleInputCommandNeighbors();
    void handlePeerCommandPing(const std::string &origin, Type type, long timestamp);
    void handleInputCommandRoute(const std::string &targetNickname);
    void handlePeerCommandAddConnection(const json &payload);
    void handlePeerCommandRemovePeer(const std::string &payload);
};

#endif
