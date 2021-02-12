#include <iostream>
#include <regex>
#include <string>
#include <chrono>
#include <unistd.h>
#include "Client.h"
#include "Helper.h"

#pragma region Constructor

Client::Client(bool debug, uint16_t multicastPort, uint16_t peerPort, const std::string &nickname) :
        nickname(nickname),
        network(NetworkManager(multicastPort, peerPort)),
        logger(Logger::getInstance()),
        topology(Topology(network.getHostname())) {
    logger.log("Welcome to P2P Chat!");
    // set debug mode
    logger.setDebug(debug);
    // Add self to the IpManager
    ips.add(network.getHostname(), network.getIp());
}

#pragma endregion

/**
 * Start the infinite client loop.
 */
void Client::start() {
    logger.log("Starting discovery for an existing network.");

    // open the socket for others to connect
    network.createPeerPollSocket();
    network.sendDiscoveryMessage();

    // accept connections for a specific timeout
    if (network.acceptPeerConnection()) {
        logger.log("Waiting for the current network topology.");
        receiveNetworkData();
        logger.log("Successfully joined an existing network.");
    } else {
        logger.log("No other peer connected. Creating a new network.");
        // add self to nicknames
        if (nickname.empty()) {
            nickname = nicknames.generateRandomNickname();
            logger.log("Your passed nickname was empty or already taken. Taking '" + nickname + "' now.");
        }

        nicknames.add(network.getHostname(), nickname);
    }

    network.createMulticastSocket();

    json j;
    // Infinite loop processing the user input and receiving messages from the sockets
    while (true) {
        processInput();
        if ((j = network.processMulticastSocket()) != nullptr) processMulticastMessage(j);
        if ((j = network.processPeerSockets()) != nullptr) processPeerMessage(j);
    }
}

/**
 * Process all queued commands
 */
void Client::processInput() {
    while (!inputCommandQueue.empty()) {
        std::string command = inputCommandQueue.front();
        // remove the processed command
        inputCommandQueue.pop();

        // remove leading and trailing spaces
        trim(command);

        if (command.empty()) continue;

        // Command type is case insensitive
        std::regex commandRegex;
        commandRegex.assign(
                R"(^/(quit|list|neighbors|plot|getkeypair|((leave|nick|gettopic|getmembers|getpublickey)\s+[\w\d]+)|((settopic|msg)\s+[\w\d]+\s+.+)|((route|help)\s*[\w\d]*)|(ping\s+[\w\d\:]+)|(join\s+[\w\d]+\s+[\w\d]+))$)",
                std::regex::icase);

        if (!regex_match(command, commandRegex)) {
            logger.log("Invalid command entered. Try again.", LogType::ERROR);
            continue;
        }

        // parse
        std::string typeString, target, text;
        int pos = command.find(' ');
        // start at 1 to ignore the '/'
        typeString = rtrim_copy(command.substr(1, pos));
        if (pos != -1) { // check if second string was passed
            command.erase(0, pos + 1);
            ltrim(command);
            pos = command.find(' ');
            target = rtrim_copy(command.substr(0, pos));
            if (pos != -1) // check if a third string was passed
            {
                command.erase(0, pos + 1);
                ltrim(command);
                text = command;
            }
        }
        processCommand(convertToType(typeString), target, text);
    }
}

/**
 * Process a input command.
 * @param type
 * @param target
 * @param text
 */
void Client::processCommand(Type type, std::string &target, const std::string &text) {
    json payload;
    std::set<std::string> nextHops;
    switch (type) {
        // commands processed locally
        case Type::GETTOPIC:
            handleInputCommandGetTopic(target);
            return;
        case Type::LIST:
            handleInputCommandList();
            return;
        case Type::GETMEMBERS:
            handleInputCommandGetMembers(target);
            return;
        case Type::NEIGHBORS:
            handleInputCommandNeighbors();
            return;
        case Type::ROUTE:
            handleInputCommandRoute(target);
            return;
        case Type::PLOT: {
            topology.plot();
            char buffer[FILENAME_MAX];
            getcwd(buffer, sizeof(buffer));
            std::string path(buffer);
            logger.log("Plot saved at '" + path + "/plot.png'.");
            return;
        }
        case Type::GETPUBLICKEY:
            handleInputCommandGetPublicKey(target);
            return;
        case Type::GETKEYPAIR:
            logger.log("Own public key:\n" + network.getPublicKey(network.getHostname()) + "\nOwn private key:\n" +
                       network.getPrivateKey());
            return;
        case Type::HELP: {
            handleInputCommandHelp();
            return;
        }
        case Type::QUIT:
            handleInputCommandQuit();
            return;

            // commands to be send to recipient
        case Type::MSG: {
            Group *group = groups.get(target);
            if (group != nullptr) {
                if (!group->isMember(network.getHostname())) {
                    logger.log("You are not a member of that group. You have to join before sending messages.",
                               LogType::WARN);
                    return;
                }
                if ((nextHops = getNextHops(target, false, true)).empty()) return;
                // encrypt the message with the group key
                payload["text"] = network.groupEncrypt(text, target);
            } else {
                std::string hostname = nicknames.reverseLookup(target);
                if (hostname.empty()) {
                    logger.log("The target is neither a group- nor a nickname.", LogType::WARN);
                    return;
                }
                if (hostname == network.getHostname()) {
                    logger.log("Why would you message yourself?", LogType::WARN);
                    return;
                }
                if ((nextHops = getNextHops(hostname, true, false)).empty()) return;
                // replace passed nickname with hostname
                target = hostname;
                // encrypt the message with the targets public key
                payload["text"] = network.publicEncrypt(text, target);
            }
            break;
        }
        case Type::PING: {
            std::string hostname = nicknames.reverseLookup(target);
            if (hostname.empty()) {
                hostname = ips.reverseLookup(target);
                if (hostname.empty()) {
                    logger.log("Unknown nickname or ip entered.", LogType::WARN);
                    return;
                }
            }
            if (hostname == network.getHostname()) {
                logger.log("You cannot ping yourself.", LogType::WARN);
                return;
            }
            if ((nextHops = getNextHops(hostname, true, false)).empty()) return;
            // replace passed nickname with hostname
            target = hostname;
            // use milliseconds
            payload["start"] = (long) std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            break;
        }
            // commands to be broadcasted
        case Type::NICK: {
            if (!NicknameManager::checkNickname(target)) {
                logger.log(
                        "Invalid nickname. It can contain letters and numbers. It has to have at least one character and up to nine.",
                        LogType::WARN);
                return;
            }
            if (!nicknames.reverseLookup(target).empty() || groups.get(target) != nullptr) {
                logger.log("Chosen nickname is already taken.", LogType::WARN);
                return;
            }
            break;
        }
        case Type::SETTOPIC: {
            Group *group = groups.get(target);
            if (group == nullptr) {
                logger.log("Failed to set topic of unknown group '" + target + "'.", LogType::WARN);
                return;
            }
            if (group->getAdmin() != network.getHostname()) {
                logger.log("Failed to set topic of group '" + target + "'. You are not the admin.", LogType::WARN);
                return;
            }
            // send to all because everyone needs this information
            nextHops = network.getNeighbors();
            payload["text"] = text;
            // change topic directly
            group->setTopic(text);
            break;
        }
        case Type::LEAVE: {
            auto group = groups.get(target);
            if (group == nullptr) {
                logger.log("Failed to leave unknown group '" + target + "'.", LogType::WARN);
                return;
            } else if (!group->isMember(network.getHostname())) {
                logger.log("You cannot leave a group you are not a member of.", LogType::WARN);
                return;
            }
            break;
        }
        case Type::JOIN: {
            auto group = groups.get(target);
            if (group == nullptr) {
                if (!nicknames.reverseLookup(target).empty()) {
                    logger.log("Group '" + target + "' does not exist, but a peer has this name.", LogType::WARN);
                    return;
                }
                logger.log("Group '" + target + "' does not exist. Trying to create it.");
                type = Type::CREATE;
            } else if (group->isMember(network.getHostname())) {
                logger.log("You are already a member of group '" + target + "'.", LogType::WARN);
                return;
            }
            // save passed group key for encryption/decryption
            network.setGroupKey(target, text);
            break;
        }
        default:
            logger.log("Invalid command entered. This should never happen...", LogType::WARN);
            return;
    }

    payload["target"] = target;
    json message = network.sendCommand(type, payload, nextHops);
    if (message != nullptr) {
        messages.addProposal(message);
        // directly execute proposals if only one peer connected
        if (topology.getPeerCount() == 1) {
            executeProposal((std::string) message["id"]);
        }
    }
}

/**
 * Get the next hops needed to reach the recipients.
 * @param recipient hostname or groupname
 * @param checkHostname true: recipient can be a hostname
 * @param checkGroupname true: recipient can be a groupname
 * @return set of next hops for a host or all members of a group
 */
std::set<std::string> Client::getNextHops(const std::string &recipient, bool checkHostname, bool checkGroupname) {
    std::set<std::string> nextHops;
    Group *group;

    if (checkGroupname && (group = groups.get(recipient)) != nullptr) {
        for (const auto &member : group->getMembers()) {
            nextHops.insert(topology.getPeer(member)->nextHop);
        }
    } else if (checkHostname && !nicknames.get(recipient).empty()) {
        nextHops.insert(topology.getPeer(recipient)->nextHop);
    }

    // erase this client from next hops
    nextHops.erase(network.getHostname());

    return nextHops;
}

/**
 * Process the json received from the multicast socket.
 */
void Client::processMulticastMessage(json &message) {
    auto bridgePeers = topology.calculateBridgePeer();
    std::string ip = message["ip"];
    logger.log("Received multicast message from '" + ip + "'.", LogType::DEBUG);
    if (std::find(bridgePeers.begin(), bridgePeers.end(), network.getHostname()) == bridgePeers.end()) {
        logger.log("Other peers have to connect to the new peer.", LogType::DEBUG);
        return;
    }
    logger.log("Connecting to new peer at '" + ip + "'.");

    auto hostname = network.connectToPeer(ip, std::to_string((int) message["port"]));

    // the first peer should send the network data to the new peer
    if (bridgePeers.at(0) == network.getHostname() && !hostname.empty()) {
        json payload{
                {"topology",  topology.toJson()},
                {"ips",       ips.toJson()},
                {"nicknames", nicknames.toJson()},
                {"groups",    groups.toJson()},
                {"crypto",    network.cryptoToJson()}
        };

        // save the public key of the new peer, so the INIT command is encrypted
        network.addPublicKey(hostname, message["publicKey"]);

        network.sendCommand(Type::INIT, payload, {hostname});
    }
}

/**
 * Process the json received from a peer.
 */
void Client::processPeerMessage(json &message) {
    logger.log("Received message: " + message.dump(), LogType::DEBUG);
    // check already received messages
    if (messages.checkReceivedStatus((std::string) message["id"])) return;

    if ((bool) message["proposal"]) {
        processProposal(message);
        return;
    }

    std::set<std::string> nextHops;
    // message is not a proposal
    switch (static_cast<Type>(message["type"])) {
        case Type::REMOVEPEER:
            // first broadcast
            nextHops = network.getNeighbors();
            nextHops.erase((std::string) message["receivedFrom"]); // remove the hop the message came from
            network.forwardMessage(message, nextHops);

            handlePeerCommandRemovePeer((std::string) message["payload"]);
            break;
        case Type::ADDCONNECTION:
            handlePeerCommandAddConnection(message["payload"]);
            // broadcast this message
            nextHops = network.getNeighbors();
            nextHops.erase((std::string) message["receivedFrom"]); // remove the hop the message came from
            network.forwardMessage(message, nextHops);
            break;
        case Type::SETTOPIC:
            handlePeerCommandSetTopic((std::string) message["origin"], (std::string) message["payload"]["target"],
                                      (std::string) message["payload"]["text"]);
            // broadcast this message
            nextHops = network.getNeighbors();
            nextHops.erase((std::string) message["receivedFrom"]); // remove the hop the message came from
            network.forwardMessage(message, nextHops);
            break;
        case Type::MSG:
            // check if this peer is member of the group or recipient of this message
            if (isRecipient(network.getHostname(), (std::string) message["payload"]["target"])) {
                handlePeerCommandMsg((std::string) message["origin"], (std::string) message["payload"]["target"],
                                     (std::string) message["payload"]["text"]);
                // do not forward, if this client is the recipient
                if ((std::string) message["payload"]["target"] == network.getHostname()) break;
            }
            // forward message
            nextHops = getNextHops((std::string) message["payload"]["target"], true, true);
            nextHops.erase((std::string) message["receivedFrom"]); // remove the hop the message came from
            network.forwardMessage(message, nextHops);
            break;
        case Type::PING:
        case Type::PONG:
            if (network.getHostname() == (std::string) message["payload"]["target"])
                handlePeerCommandPing((std::string) message["origin"], static_cast<Type>(message["type"]),
                                      message["payload"]["start"]);
            else {
                nextHops = getNextHops((std::string) message["payload"]["target"], true, false);
                network.forwardMessage(message, nextHops);
            }
            break;
        default:
            // unknown commands or the ones that should never occur here
            logger.log("Cannot process command type that is unknown or supposed to be processed locally.",
                       LogType::ERROR);
            return;
    }
}

/**
 * Process a received proposal message.
 * @param message
 */
void Client::processProposal(json &message) {
    // ignore own proposals
    if (((std::string) message["origin"]) == network.getHostname()) return;

    Type messageType = static_cast<Type>(message["type"]);
    // next hops for broadcast
    auto nextHops = network.getNeighbors();
    nextHops.erase((std::string) message["receivedFrom"]); // remove the hop the message came from
    // forward the proposal to everyone
    network.forwardMessage(message, nextHops);

    bool confirm = true;
    // check if join is valid
    switch (messageType) {
        case Type::CONFIRMATION: {
            // received a confirmation. If the number of confirmations equals the number of connected clients,
            // we can execute the proposal
            if (messages.addProposalConfirmation((std::string) message["payload"], (std::string) message["origin"]) ==
                topology.getPeerCount() - 1) {
                executeProposal((std::string) message["payload"]);
            }
            return;
        }
        case Type::REJECT: {
            messages.removeProposal((std::string) message["payload"]);
            return;
        }
        case Type::JOIN: {
            if (groups.get((std::string) message["payload"]["target"]) == nullptr) {
                logger.log("Received join proposal for not existing group.", LogType::DEBUG);
                confirm = false;
            }
            break;
        }
        case Type::CREATE: {
            if (groups.get((std::string) message["payload"]["target"]) != nullptr) {
                logger.log("Received create proposal for existing group.", LogType::DEBUG);
                confirm = false;
            }
            break;
        }
        case Type::LEAVE: {
            if (groups.get((std::string) message["payload"]["target"]) == nullptr) {
                logger.log("Received leave proposal for existing group.", LogType::DEBUG);
                confirm = false;
            }
            break;
        }
        case Type::NICK: {
            if (!nicknames.reverseLookup((std::string) message["payload"]["target"]).empty()) {
                logger.log("Received nick proposal for taken nickname.", LogType::DEBUG);
                confirm = false;
            }
            break;
        }
        default:
            return;
    }

    // if previous checks were ok, check existing proposals
    if (confirm) {
        if (messages.checkProposalBlocked(message)) {
            logger.log("Received proposal that is blocked by another proposal.", LogType::DEBUG);
            confirm = false;
        } else {
            // add to received proposal
            if ((confirm = messages.addProposal(message))) {
                // confirm newly received proposal
                if (messages.addProposalConfirmation((std::string) message["id"], network.getHostname()) ==
                    topology.getPeerCount() - 1) {
                    executeProposal((std::string) message["id"]);
                }
            }
        }
    }

    logger.log("Sending " + std::string(confirm ? "Confirmation" : "Reject") + " for proposal " +
               (std::string) message["id"], LogType::DEBUG);
    network.sendCommand(confirm ? Type::CONFIRMATION : Type::REJECT, (std::string) message["id"],
                        network.getNeighbors());
}

/**
 * Execute a stored proposal,
 * @param id Message id of the proposal
 */
void Client::executeProposal(const std::string &id) {
    auto json = messages.getProposal(id);
    // remove executed proposal
    messages.removeProposal(id);

    switch (static_cast<Type>(json["type"])) {
        case Type::JOIN:
            handlePeerCommandJoin((std::string) json["origin"], (std::string) json["payload"]["target"]);
            break;
        case Type::CREATE:
            handlePeerCommandCreate((std::string) json["origin"], (std::string) json["payload"]["target"]);
            break;
        case Type::LEAVE:
            handlePeerCommandLeave((std::string) json["origin"], (std::string) json["payload"]["target"]);
            break;
        case Type::NICK:
            handlePeerCommandNick((std::string) json["origin"], (std::string) json["payload"]["target"]);
            break;
        default:
            // unknown commands or the ones that should never occur here
            logger.log("Cannot execute proposal command type that is unknown or supposed to be processed locally.",
                       LogType::ERROR);
            return;
    }
}

/**
 * Check if host is recipient of a message.
 * @param hostname
 * @param recipient hostname or groupname
 * @return true: hostname is recipient or a member of group
 */
bool Client::isRecipient(const std::string &hostname, const std::string &recipient) {
    if (hostname == recipient) return true;
    Group *group = groups.get(recipient);
    if (group != nullptr) return group->isMember(hostname);
    return false;
}

/**
 * Wait for other peers to send us the current network data and process it.
 * Should be done after startup.
 */
void Client::receiveNetworkData() {
    while (true) {
        auto j = network.processPeerSockets();
        if (j == nullptr) continue;

        // Ignore all other messages, as long as we didn't receive the data
        if (((Type) j.value("type", Type::INVALID)) != Type::INIT) continue;

        // load topology
        topology.loadJson(j["payload"]["topology"]);
        // load ips
        ips.loadJson(j["payload"]["ips"]);
        // load nicknames
        nicknames.loadJson(j["payload"]["nicknames"]);
        // load groups
        groups.loadJson(j["payload"]["groups"]);
        // load crypto
        network.cryptoLoadJson(j["payload"]["crypto"]);

        json connections;

        // add new links to neighbors of this peer
        auto neighbors = network.getNeighbors();
        for (auto const &neighbor: neighbors) {
            topology.setConnection(network.getHostname(), neighbor, true);
            connections.push_back({network.getHostname(), neighbor}); // create json with new connections
        }

        // Check passed nickname
        if (nickname.empty() || !nicknames.reverseLookup(nickname).empty()) {
            nickname = nicknames.generateRandomNickname();
            logger.log("Your passed nickname was empty or already taken. Taking '" + nickname + "' now.");
        }
        nicknames.add(network.getHostname(), nickname);

        // Broadcast new connection between this and the neighbors to the network
        network.sendCommand(Type::ADDCONNECTION, {
                {"connections", connections},
                {"newPeers",    {{
                                         network.getHostname(), {
                                                                        {"ip", network.getIp()},
                                                                        {"name", nickname},
                                                                        {"publicKey", network.getPublicKey(
                                                                                network.getHostname())}
                                                                }
                                 }}
                }
        }, neighbors);
        return;
    }
}

/**
 * Handles the reconnect in case a disconnect of a peer causes a network fracture. Some peers do an active
 * reconnect and other just wait for peer connections.
 */
void Client::handleNetworkFracture() {
    auto newConnectionTargets = topology.calculateNewConnections();
    if (newConnectionTargets.empty()) {
        logger.log("The network is fractured! Waiting for other peers to do the reconnect.");
        network.acceptPeerConnection(3);
    } else {
        logger.log("The network is fractured! Trying to rescue the network.");
        // connect to the targets
        for (const auto &target: newConnectionTargets) {
            network.connectToPeer(ips.get(target));
        }
        json connections;
        for (auto const &target: newConnectionTargets) {
            topology.setConnection(network.getHostname(), target, true);
            connections.push_back({network.getHostname(), target}); // create json with new connections
        }
        // Broadcast new connection between this and the targets
        network.sendCommand(Type::ADDCONNECTION, {
                {"connections", connections}
        }, network.getNeighbors());
    }
}

/**
 * Handles the reconnect in case a disconnect of a peer causes a network underconnection. Some peers do an active
 * reconnect and other just wait for peer connections.
 */
void Client::handleNetworkUnderconnected() {
    auto target = topology.calculateNewUnderconnections();
    if (target.empty()) {
        logger.log("The network is underconnected. Waiting for other peers to do the reconnect.");
        network.acceptPeerConnection(3);
    } else {
        logger.log("The network is underconnected! Trying to rescue the network.");
        // connect to the targets
        network.connectToPeer(ips.get(target));
        json connections;
        topology.setConnection(network.getHostname(), target, true);
        connections.push_back({network.getHostname(), target}); // create json with new connection
        // Broadcast new connection between this and the targets
        network.sendCommand(Type::ADDCONNECTION, {
                {"connections", connections}
        }, network.getNeighbors());
    }
}

#pragma region handle commands

#pragma region Peer Commands

/**
 * Remove a disconnected peer.
 * @param payload Probably disconnected peer
 */
void Client::handlePeerCommandRemovePeer(const std::string &payload) {
    if (nicknames.get(payload).empty()) return; // ignore unknown peers

    logger.log("Peer ('" + nicknames.get(payload) + "') lost connection. Removing it.");
    // remove message id
    messages.removeMessageId(payload);
    // remove from groups, topology, nicknames, ips
    topology.removePeer(payload);

    auto leftGroups = groups.removeFromAllGroups(payload);
    for (const auto &groupName: leftGroups) {
        logger.log("Removed member ('" + nicknames.get(payload) + "') from group '" + groupName + "'.");
        auto currentGroup = groups.get(groupName);
        if (currentGroup->hasChangedAdmin() && currentGroup->getAdmin() == network.getHostname())
            logger.log("You are the new admin of group '" + groupName + "'.");
    }
    auto removedGroups = groups.removeEmptyGroups();
    for (const auto &groupName: removedGroups)
        logger.log("Last member ('" + nicknames.get(payload) + "') left group '" + groupName +
                   "'. Removing the group.");

    nicknames.remove(payload);
    ips.remove(payload);

    // check if the network needs reconnects
    if (topology.isFractured()) handleNetworkFracture();
    else if (topology.isUnderconnected()) handleNetworkUnderconnected();
}

/**
 * Add new connected peers and/or add connection between peers
 * @param payload contains new connections at "connection" key and "newPeers" key contains new peers
 */
void Client::handlePeerCommandAddConnection(const json &payload) {
    if (payload.contains("newPeers")) {
        json newPeers = payload["newPeers"];
        for (const auto &item : newPeers.items()) {
            const std::string &currentHostname = item.key();
            topology.addPeer(currentHostname);
            nicknames.add(currentHostname, (std::string) item.value()["name"]);
            ips.add(currentHostname, (std::string) item.value()["ip"]);
            network.addPublicKey(currentHostname, (std::string) item.value()["publicKey"]);
            logger.log("Added new peer (Hostname: '" + currentHostname + "').", LogType::DEBUG);
            logger.log("Peer ('" + nicknames.get(currentHostname) + "') joined the chat.");
        }
    }
    if (payload.contains("connections")) {
        json connections = payload["connections"];
        for (const auto &item : connections.items()) {
            topology.setConnection((std::string) item.value()[0], (std::string) item.value()[1], true);
            logger.log("Added new connection between '" + (std::string) item.value()[0] + "' and '" +
                       (std::string) item.value()[1] + "'.", LogType::DEBUG);
        }
    }
}

/**
 * Execute this after all confirmations received. Adds the hostname to a group.
 * @param hostname
 * @param groupname
 */
void Client::handlePeerCommandJoin(const std::string &hostname, const std::string &groupname) {
    const auto group = groups.get(groupname);
    if (group != nullptr) {
        group->addMember(hostname);
        logger.log("Peer ('" + nicknames.get(hostname) + "') joined group '" + groupname + "'.");
    } else
        logger.log("Peer ('" + nicknames.get(hostname) + "') can't join unknown group '" + groupname + "'.",
                   LogType::DEBUG);
}

/**
 * Execute this after all confirmations received. Create the group.
 * @param hostname admin of the group
 * @param groupname of the new group
 */
void Client::handlePeerCommandCreate(const std::string &hostname, const std::string &groupname) {
    if (groups.create(groupname, hostname) != nullptr)
        logger.log("Peer ('" + nicknames.get(hostname) + "') created group '" + groupname + "'.");
    else
        logger.log("Peer ('" + nicknames.get(hostname) + "') failed creating group '" + groupname + "'.",
                   LogType::DEBUG);
}

/**
 * Execute this after all confirmations received. Remove the hostname from the group.
 * @param hostname
 * @param groupname
 */
void Client::handlePeerCommandLeave(const std::string &hostname, const std::string &groupname) {
    const auto group = groups.get(groupname);
    if (group != nullptr) {
        group->removeMember(hostname);
        logger.log("Peer ('" + nicknames.get(hostname) + "') left group '" + groupname + "'.");
        if (group->hasChangedAdmin() && group->getAdmin() == network.getHostname())
            logger.log("You are the new admin of group '" + groupname + "'.");
        auto removedGroups = groups.removeEmptyGroups();
        for (const auto &groupName: removedGroups)
            logger.log("Last member ('" + nicknames.get(hostname) + "') left group '" + groupName +
                       "'. Removing the group.");
    } else
        logger.log("Peer ('" + nicknames.get(hostname) + "') can not leave unknown group '" + groupname + "'.",
                   LogType::DEBUG);
}

/**
 * Execute this after all confirmations received. Renames a client.
 * @param hostname of the target
 * @param nick
 */
void Client::handlePeerCommandNick(const std::string &hostname, const std::string &nick) {
    const auto oldNick = nicknames.get(hostname);
    if (nicknames.rename(hostname, nick))
        logger.log("Peer ('" + oldNick + "') changed nick to '" + nick + "'.");
    else
        logger.log("Failed to change nick of Peer ('" + oldNick + "').", LogType::DEBUG);
}

/**
 * Directly execute set topic.
 * @param hostname of the peer the message came from
 * @param groupname
 * @param text
 */
void
Client::handlePeerCommandSetTopic(const std::string &hostname, const std::string &groupname, const std::string &text) {
    auto group = groups.get(groupname);
    if (group == nullptr) {
        logger.log("Peer ('" + nicknames.get(hostname) + "') tried to set topic of unknown group '" + groupname + "'.",
                   LogType::DEBUG);
        return;
    }
    if (group->getAdmin() != hostname) {
        logger.log(
                "Peer ('" + nicknames.get(hostname) + "') tried to set topic of group '" + groupname +
                "', but is not admin.", LogType::DEBUG);
        return;
    }
    logger.log("Peer ('" + nicknames.get(hostname) + "') set topic of group '" + groupname + "' to '" + text + "'.");
    group->setTopic(text);
}

/**
 * Handle message directly.
 * @param hostname hostname the message came from
 * @param recipient target group or peer
 * @param text
 */
void Client::handlePeerCommandMsg(const std::string &hostname, const std::string &recipient, const std::string &text) {
    if (groups.get(recipient) != nullptr) {
        auto message = network.groupDecrypt(text, recipient);
        logger.log("[" + recipient + "] " + nicknames.get(hostname) +
                   (message.empty() ? " used another key for encryption." : (": " + message)), LogType::MESSAGE);
    } else
        logger.log(nicknames.get(hostname) + ": " + network.privateDecrypt(text), LogType::MESSAGE);
}

/**
 * Handle ping and pong if this peer is the target of the command.
 * @param origin of the ping or pong
 * @param type Ping or Pong
 * @param timestamp PING: timestamp from the message. PONG: timestamp from ping message
 */
void Client::handlePeerCommandPing(const std::string &origin, Type type, long timestamp) {
    if (type == Type::PING) {
        // send PONG to origin and copy original ping timestamp
        network.sendCommand(Type::PONG, {
                {"target", origin},
                {"start",  timestamp}
        }, getNextHops(origin, true, false));
    } else {
        long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        logger.log("Ping to Peer ('" + nicknames.get(origin) + "') is " + std::to_string(now - timestamp) + "ms.");
    }
}

#pragma endregion

#pragma region Input Commands

/**
 * Directly list the current existing groups.
 */
void Client::handleInputCommandList() {
    std::string listedGroups = groups.toString();
    if (listedGroups.empty()) {
        logger.log("There are currently no groups.");
    } else {
        listedGroups = listedGroups.substr(0, listedGroups.size() - 2); // remove ', ' from last entry
        logger.log("Groups: " + listedGroups);
    }
}

/**
 * Directly get the topic of the group.
 * @param groupname
 */
void Client::handleInputCommandGetTopic(const std::string &groupname) {
    Group *group = groups.get(groupname);

    if (group == nullptr)
        logger.log("Failed to get topic of unknown group '" + groupname + "'.", LogType::WARN);
    else
        logger.log("Topic: '" + group->getTopic() + "'.");
}

/**
 * Directly close all sockets and exit the program.
 */
void Client::handleInputCommandQuit() {
    logger.log("Leaving the chat. Bye!");
    network.closeAllSockets();
    logger.outputExit(EXIT_SUCCESS);
}

/**
 * Directly list the members of the group.
 * @param groupname
 */
void Client::handleInputCommandGetMembers(const std::string &groupname) {
    Group *group = groups.get(groupname);

    if (group == nullptr) {
        logger.log("Failed to list members of unknown group '" + groupname + "'.", LogType::WARN);
        return;
    }

    std::string members;
    for (const auto &hostname : group->getMembers()) {
        members += nicknames.get(hostname) + ", ";
    }
    members = members.substr(0, members.size() - 2); // remove ', ' from last entry
    logger.log("Members: " + members);
}

/**
 * Directly output neighbors of this peer.
 */
void Client::handleInputCommandNeighbors() {
    std::string neighbors;
    for (const auto &hostname : network.getNeighbors()) {
        neighbors += hostname + ", ";
    }
    if (neighbors.empty()) {
        logger.log("There are currently no neighbors.");
    } else {
        neighbors = neighbors.substr(0, neighbors.size() - 2); // remove ', ' from last entry
        logger.log("Neighbors: " + neighbors);
    }
}

/**
 * Directly show route to a destination or if hostname is empty, the routing table.
 * @param targetNickname
 */
void Client::handleInputCommandRoute(const std::string &targetNickname) {
    if (targetNickname.empty()) {
        auto routingTable = topology.getRoutingTable();
        logger.log("Routing Table: ");
        for (auto const &pair : routingTable) {
            if (pair.first == network.getHostname()) continue; // skip own route
            logger.log("Peer: '" + nicknames.get(pair.first) + "', next hop: '" + nicknames.get(pair.second) + "'");
        }
        return;
    }

    // check if host is known
    std::string hostname = nicknames.reverseLookup(targetNickname);
    if (hostname.empty()) {
        logger.log("Unknown nickname passed.", LogType::WARN);
        return;
    }
    auto path = topology.getShortestPath(hostname);
    std::string pathString;
    for (const auto &hop : path) {
        pathString += nicknames.get(hop) + " -> ";
    }
    pathString = pathString.substr(0, pathString.size() - 4);   // remove ' -> ' from last entry
    logger.log("Path: " + pathString);
}

/**
 * Directly list the public key of a specific peer.
 * @param targetNickname
 */
void Client::handleInputCommandGetPublicKey(const std::string &targetNickname) {
    std::string hostname = nicknames.reverseLookup(targetNickname);

    if (hostname.empty()) {
        logger.log("Failed to get public key of unknown nickname '" + targetNickname + "'.", LogType::WARN);
        return;
    }

    logger.log("Public key of Peer ('" + targetNickname + "'):\n" + network.getPublicKey(hostname));
}

/**
 * Directly show all available commands.
 * @param targetNickname
 */
void Client::handleInputCommandHelp() {
    logger.log("Available commands:");
    logger.log("JOIN <name> <key>: Join/Create a group and encrypt messages with the passed key");
    logger.log("LEAVE <name>: Leave the group");
    logger.log("NICK <name>: Change own nickname");
    logger.log("LIST: List all existing groups");
    logger.log("GETMEMBERS <name>: Lists all users of the group");
    logger.log("GETTOPIC <name>: Prints the current topic of the group");
    logger.log("SETTOPIC <name> <text>: Sets the current topic of the group");
    logger.log("MSG <name> <text>: Message a single user or group");
    logger.log("NEIGHBORS: Lists direct Neighbors");
    logger.log("PING <name/ip>: Determines availability and RTT to destination");
    logger.log("ROUTE <name>: Shows route to destination including individual hops or full routing table");
    logger.log("PLOT: Plots topology of the network to a file");
    logger.log("GETPUBLICKEY <name>: Print the public key of a specific peer");
    logger.log("GETKEYPAIR: Print the currently used public and private key");
    logger.log("QUIT: Leave P2P Chat");
}

#pragma endregion

#pragma endregion

#pragma region IO Operations

/**
 * Add a command to the inputCommandQueue.
 * @param command
 */
void Client::pushCommand(const std::string &command) {
    inputCommandQueue.push(command);
}

/**
 * Shows if the client has messages to output.
 * @return true = has messages
 */
bool Client::hasOutput() {
    return logger.hasOutput();
}

/**
 * Pop the first message from the outputMessageQueue.
 * @return The popped message
 */
std::string Client::popOutputMessage() {
    return logger.popOutputMessage();
}

#pragma endregion
