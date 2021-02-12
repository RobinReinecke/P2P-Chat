#include <netdb.h>
#include <cstring>
#include "NetworkManager.h"
#include "Helper.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <ifaddrs.h>

#pragma region Constructor

NetworkManager::NetworkManager(int multicastPort, int peerPort) : multicastPort(multicastPort), peerPort(peerPort),
                                                                  logger(Logger::getInstance()),
                                                                  localHostname(getLocalHostname()),
                                                                  ip(getLocalIPv6()),
                                                                  crypto(localHostname) {}

#pragma endregion

#pragma region MulticastSocket

/**
 * Create the Multicast socket all clients listen on.
 */
void NetworkManager::createMulticastSocket() {
    struct addrinfo hints{}, *addressInfo, *multicastInfo;

    // resolve multicast group address
    hints.ai_family = AF_INET6;
    hints.ai_flags = AI_NUMERICHOST;
    getaddrinfo(multicastAddr.c_str(), nullptr, &hints, &multicastInfo);

    // configure socket options
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6; // IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP
    hints.ai_flags = AI_PASSIVE;

    // Translate name of a service location to set of socket addresses into the addressInfo variable
    getaddrinfo(nullptr, std::to_string(multicastPort).c_str(), &hints, &addressInfo);

    int multicastSocket;
    // create the multicast socket
    if ((multicastSocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)) < 0) {
        logger.log("Failed to create multicast socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    if (bind(multicastSocket, addressInfo->ai_addr, addressInfo->ai_addrlen) < 0) {
        logger.log("Failed to bind multicast socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    struct ipv6_mreq group{};
    group.ipv6mr_interface = 0;
    group.ipv6mr_multiaddr = ((struct sockaddr_in6 *) (multicastInfo->ai_addr))->sin6_addr;
    if (setsockopt(multicastSocket, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
        logger.log("Failed to set multicast socket options.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }


    // add to poll socket
    multicastPollSocket = (pollfd *) malloc(sizeof(struct pollfd));
    multicastPollSocket->fd = multicastSocket;
    multicastPollSocket->events = POLLIN;

    logger.log(
            "Successfully opened multicast socket on '" + multicastAddr + "', port " + std::to_string(multicastPort) +
            ".");
}

/**
 * Check the multicast socket for new messages.
 * @returns received message json or nullptr if nothing received.
 */
json NetworkManager::processMulticastSocket() {
    const int pollCount = poll(multicastPollSocket, 1, 1);
    if (pollCount == 0) {
        return nullptr;
    }
    if (pollCount == -1) {
        logger.log("Failed to poll from multicast socket", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // Check if new messages are ready to read
    if (!(multicastPollSocket->revents & POLLIN))
        return nullptr;

    int bytesRead;
    char buffer[1024];

    // Read the incoming message
    if ((bytesRead = recv(multicastPollSocket->fd, buffer, sizeof(buffer), 0)) <= 0) {
        // Master disconnected
        logger.log("Failed to recv from multicastSocket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }
    // set the string terminating NULL byte on the end of the data read
    buffer[bytesRead] = '\0';
    return tryParse(buffer);
}

/**
 * Send hello message to already existing peers.
 */
void NetworkManager::sendDiscoveryMessage() const {
    struct addrinfo hints{}, *addressInfo;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    getaddrinfo(multicastAddr.c_str(), std::to_string(multicastPort).c_str(), &hints, &addressInfo);

    int discoverySocket;
    // create the discovery socket to send the message
    if ((discoverySocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, 0)) < 0) {
        logger.log("Failed to create discovery socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // own ip address, peerPort
    json j{
            {"ip",   ip},
            {"port", peerPort},
            {"publicKey", crypto.get(localHostname)}
    };

    const auto message = j.dump();

    if (sendto(discoverySocket, message.c_str(), message.length(), 0, addressInfo->ai_addr, addressInfo->ai_addrlen) ==
        -1) {
        logger.log("Failed to send discovery message.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    close(discoverySocket);
}

#pragma endregion

#pragma region PeerSockets

/**
 * Create the poll socket for other Peers to connect.
 */
void NetworkManager::createPeerPollSocket() {
    const int yes = 1;
    struct addrinfo hints{}, *addressInfo;

    // configure socket options
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6; // IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // fill own IP
    // Translate name of a service location to set of socket addresses into the res variable
    getaddrinfo(nullptr, std::to_string(peerPort).c_str(), &hints, &addressInfo);

    int peerSocket;
    // create the socket which listens for peer connections
    if ((peerSocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)) < 0) {
        logger.log("Failed to create peer socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // allow socket to accept multiple connections
    if (setsockopt(peerSocket, SOL_SOCKET, SO_REUSEADDR, (char *) &yes, sizeof(yes)) < 0) {
        logger.log("Failed to setsockopt for peer socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // bind socket to the clients address and defined port
    if ((bind(peerSocket, addressInfo->ai_addr, addressInfo->ai_addrlen) < 0)) {
        logger.log("Failed to bind peer socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // listen for incoming connections. Maximum of 3 queued connections
    if (listen(peerSocket, 3) < 0) {
        logger.log("Failed to listen on peer socket.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    // add to clientPollSockets
    peerPollSockets[0].fd = peerSocket;
    peerPollSockets[0].events = POLLIN;
    peerSocketsCount++;

    logger.log("Waiting for peers to connect on port " + std::to_string(peerPort) + ".");
}

/**
 * Connect to a new peer.
 * @param peerIp
 * @param port
 * @return hostname of the new peer or empty string if something went wrong
 */
std::string NetworkManager::connectToPeer(const std::string &peerIp, std::string port) {
    struct addrinfo hints{}, *addressInfo;

    // if no port was passed, we try to get it from the stored ones
    if (port.empty()) {
        auto iterator = hostnamePort.find(ips.reverseLookup(peerIp));
        if (iterator == hostnamePort.end()) {
            port = std::to_string(peerPort); // fallback
        } else {
            port = std::to_string(iterator->second);
        }
    }

    // configure socket options
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6; // IPv6
    hints.ai_socktype = SOCK_STREAM;
    // Translate name of a service location to set of socket addresses into the res variable
    getaddrinfo(peerIp.c_str(), port.c_str(), &hints, &addressInfo);

    int newPeerSocket;
    if ((newPeerSocket = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol)) < 0) {
        logger.log("Failed to create peer socket.", LogType::ERROR);
        return "";
    }

    struct timeval timeout{};
    timeout.tv_sec = 7; // after 7 seconds connect() will timeout
    timeout.tv_usec = 0;
    if (setsockopt(newPeerSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger.log("Failed to setsockopt for peer socket.", LogType::ERROR);
        return "";
    }

    if (connect(newPeerSocket, addressInfo->ai_addr, addressInfo->ai_addrlen) < 0) {
        logger.log("Failed to connect to peer socket at '" + peerIp + "'.", LogType::ERROR);
        return "";
    }

    addToPeerPollSockets(newPeerSocket);

    // get hostname
    char peerHostname[50];
    addrinfo *res;
    // get addr infos to fill the addrinfo struct
    getaddrinfo(peerIp.c_str(), nullptr, nullptr, &res);
    // reverse lookup hostname of the host
    getnameinfo(res->ai_addr, res->ai_addrlen, peerHostname, sizeof(peerHostname), nullptr, 0, 0);

    hostnameSockets.emplace(peerHostname, newPeerSocket);
    // save ip and port for a potential reconnect
    ips.add(peerHostname, peerIp);
    hostnamePort.emplace(peerHostname, std::stoi(port));

    logger.log("Connected to new peer (Hostname: '" + std::string(peerHostname) + "').");
    return peerHostname;
}

/**
 * Accept new peer connections for a specific timeout.
 * @param timeout timeout in seconds
 * @return true = min one peer connected
 */
bool NetworkManager::acceptPeerConnection(int timeout) {
    bool gotConnection = false;

    const auto timeoutTimestamp = std::time(nullptr) + timeout;
    while (std::time(nullptr) <= timeoutTimestamp && peerSocketsCount < 3) {
        // only poll from the first peer socket
        const int pollCount = poll(peerPollSockets, 1, 1);
        if (pollCount == 0) continue;

        if (pollCount == -1) {
            logger.log("Failed to poll from peerPollSockets", LogType::ERROR);
            logger.outputExit(EXIT_FAILURE);
        }

        // Check if connection requests are ready to read
        if (!(peerPollSockets[0].revents & POLLIN)) continue;

        int newPeerSocket;
        struct sockaddr_in6 newAddr{};
        socklen_t newAddrSize = sizeof(newAddr);
        // accept new client connection
        if ((newPeerSocket = accept(peerPollSockets[0].fd, (struct sockaddr *) &newAddr, &newAddrSize)) < 0) {
            logger.log("Failed to accept connection from peerPollSockets", LogType::ERROR);
            logger.outputExit(EXIT_FAILURE);
        }

        addToPeerPollSockets(newPeerSocket);

        // get IP address
        char peerIP[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &newAddr.sin6_addr, peerIP, INET6_ADDRSTRLEN);

        // get hostname
        char peerHostname[50];
        addrinfo *res;
        // get addr infos to fill the addrinfo struct
        getaddrinfo(peerIP, nullptr, nullptr, &res);
        // reverse lookup hostname of the host
        getnameinfo(res->ai_addr, res->ai_addrlen, peerHostname, sizeof(peerHostname), nullptr, 0, 0);

        hostnameSockets.emplace(peerHostname, newPeerSocket);
        // save ip and port for a potential reconnect
        ips.add(peerHostname, peerIP);
        hostnamePort.emplace(peerHostname, newAddr.sin6_port);

        logger.log("Got new connection from peer (Hostname: '" + std::string(peerHostname) + "', IP: '" +
                   std::string(peerIP) + "').");
        gotConnection = true;
    }

    return gotConnection;
}

/**
 * Check the peer sockets for new messages.
 * @returns received message json or nullptr if nothing received.
 */
json NetworkManager::processPeerSockets() {
    const int pollCount = poll(peerPollSockets, peerSocketsCount, 1);
    if (pollCount == 0) return nullptr;

    if (pollCount == -1) {
        logger.log("Failed to poll from peer sockets", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    for (auto i = 0; i < peerSocketsCount; ++i) {
        auto currentSocket = peerPollSockets[i];
        // check if ready to read and its not a connection request
        if (!(currentSocket.revents & POLLIN) || i == 0) continue;

        // Read the incoming message
        std::string message = recvString(currentSocket.fd);
        if (message.empty()) {
            auto disconnectedPeer = reverseLookup(currentSocket.fd);
            // Peer disconnected
            logger.log("Lost connection to peer (Hostname: '" + disconnectedPeer + "').");
            removeFromPeerPollSockets(currentSocket.fd);
            hostnameSockets.erase(disconnectedPeer);
            bool successReconnect;
            int timeout = 1;
            // peer with lower hostname should try the reconnect
            if (disconnectedPeer < localHostname) {
                const auto timeoutTimestamp = std::time(nullptr) + timeout + 1;
                logger.log("Waiting " + std::to_string(timeout) + " second(s) for a the peer to reconnect.");
                successReconnect = acceptPeerConnection(timeout);
                // cooldown, to avoid false reconnects
                while (std::time(nullptr) <= timeoutTimestamp) {}
            } else {
                logger.log("Trying to reconnect to the peer for " + std::to_string(timeout + 1) + " seconds.");
                const auto timeoutTimestamp = std::time(nullptr) + timeout + 1;
                successReconnect = !connectToPeer(ips.get(disconnectedPeer),
                                                  std::to_string(hostnamePort.find(disconnectedPeer)->second)).empty();
                // wait maximum timeout to avoid overtaking the other waiting peers
                while (std::time(nullptr) <= timeoutTimestamp) {}
            }

            if (successReconnect) return nullptr; // do nothing here

            ips.remove(disconnectedPeer);
            hostnamePort.erase(disconnectedPeer);

            // remove the disconnectedPeer
            json localMessage = buildJson(false, Type::REMOVEPEER, disconnectedPeer);
            localMessage["receivedFrom"] = disconnectedPeer;
            return localMessage;
        }

        json j = tryParse(crypto.privateDecrypt(message));
        // add the hostname of the sending peer
        if (j != nullptr) j["receivedFrom"] = reverseLookup(currentSocket.fd);
        return j;
    }
    return nullptr;
}

/**
 * Close all existing sockets. Only call this if the client quits.
 */
void NetworkManager::closeAllSockets() {
    // close multicast socket
    close(multicastPollSocket->fd);

    // close peer sockets
    for (auto i = 0; i < peerSocketsCount; ++i) {
        close(peerPollSockets[i].fd);
    }
}

/**
 * Add the socket to the peer poll sockets.
 * @param socket id of the new socket
 */
void NetworkManager::addToPeerPollSockets(int socket) {
    peerPollSockets[peerSocketsCount].fd = socket;
    peerPollSockets[peerSocketsCount].events = POLLIN;
    peerSocketsCount++;
}

/**
 * Remove the socket from the peer poll sockets.
 * @param socket id of the socket
 */
void NetworkManager::removeFromPeerPollSockets(int socket) {
    for (auto i = 0; i < peerSocketsCount; i++) {
        if (peerPollSockets[i].fd != socket) continue;

        // reset the socket
        peerPollSockets[i].fd = 0;
        peerPollSockets[i].events = 0;
        peerPollSockets[i].revents = 0;

        // Copy the one from the end over this one
        peerPollSockets[i] = peerPollSockets[peerSocketsCount - 1];
        peerSocketsCount--;
        break;
    }
}

#pragma endregion

#pragma region Messages

/**
 * Send a specified command to a set of sockets
 * @param type Type of the request
 * @param payload
 * @param nextHops set of hostnames the command should be send to
 */
json
NetworkManager::sendCommand(const Type type, const json &payload, const std::set<std::string> &nextHops) {
    // get proposal confirmation for this types
    if (type == Type::CONFIRMATION || type == Type::REJECT || type == Type::NICK || type == Type::LEAVE ||
        type == Type::JOIN || type == Type::CREATE) {
        json message = buildJson(true, type, payload);
        forwardMessage(message, getNeighbors());
        return message;
    } else {
        // send command to sockets
        forwardMessage(buildJson(false, type, payload), nextHops);
        return nullptr;
    }
}

/**
 * Send a message to next hops
 * @param message
 * @param nextHops set of hostnames the message should be send to
 */
void NetworkManager::forwardMessage(const json &message, const std::set<std::string> &nextHops) {
    const auto rq = message.dump();
    for (auto nextHop : nextHops) {
        auto socket = getSocket(nextHop);
        if (!sendString(socket, crypto.publicEncrypt(rq, nextHop))) {
            logger.log("Error while sending command to another peer.", LogType::ERROR);
        }
    }
}

#pragma endregion

#pragma region Helper

/**
 * Determine the full local hostname.
 * @return Full hostname string
 */
std::string NetworkManager::getLocalHostname() {
    struct addrinfo hints{}, *info, *p;

    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; /*either IPV4 or IPV6*/
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(hostname, "http", &hints, &info) != 0) {
        logger.log("Could not determine own hostname.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    if (std::string(info->ai_canonname).empty()) {
        logger.log("Could not determine own hostname.", LogType::ERROR);
        logger.outputExit(EXIT_FAILURE);
    }

    return info->ai_canonname;
}

/**
 * Get local scope IPv6 address.
 * @return IPv6
 */
std::string NetworkManager::getLocalIPv6() {
    struct ifaddrs *ifAddrStruct = nullptr;
    struct ifaddrs *ifa;
    void *tmpAddrPtr;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr = &((struct sockaddr_in6 *) ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            auto address = std::string(addressBuffer);
            if (address.rfind("2001", 0) == 0) {
                return address;
            }
        }
    }
    exit(EXIT_FAILURE);
}

/**
 * Returns the socket for a hostname.
 * @param hostname
 * @return socket or -1 if invalid hostname
 */
int NetworkManager::getSocket(std::string &hostname) const {
    auto iterator = hostnameSockets.find(hostname);

    if (iterator == hostnameSockets.end()) return -1;
    return iterator->second;
}

/**
 * Get the connected neighbors of this peer.
 * @return Set of hostnames
 */
std::set<std::string> NetworkManager::getNeighbors() {
    std::set<std::string> neighbors;
    for (auto i = 1; i < peerSocketsCount; ++i) {
        auto neighborHostname = reverseLookup(peerPollSockets[i].fd);
        if (!neighborHostname.empty()) neighbors.insert(neighborHostname);
    }
    return neighbors;
}

/**
 * Return the hostname for a socket.
 * @return hostname or empty string if unknown
 */
std::string NetworkManager::reverseLookup(int socket) {
    for (const auto &pair : hostnameSockets) {
        if (pair.second == socket) {
            return pair.first;
        }
    }
    return "";
}

/**
 * Build a consistent json with unix timestamp for messages.
 * @param proposal Set proposal status
 * @param type Type of the message
 * @return Populated json
 */
json NetworkManager::buildJson(bool proposal, Type type, const json &payload) {
    return {
            {"id",        localHostname + '-' + std::to_string(++messageId)}, // with limiter
            {"origin",    localHostname},
            {"timestamp", std::time(nullptr)}, // set current unix timestamp
            {"proposal",  proposal},
            {"type",      type},
            {"payload",   payload}
    };
}

#pragma endregion

#pragma region Send & Receive

size_t NetworkManager::sendAll(int socket, void const *buff, size_t buffLen) {
    size_t result, total = 0;
    while (total < buffLen) {
        result = ::send(socket, (void *) buff, buffLen, 0);
        if (result < 0)
            return -1;
        else if (result == 0)
            break;
        buff = static_cast<char const *>(buff) + result;
        total += result;
    }
    return total;
}

/**
 * Send a string to a socket
 * @param socket
 * @param message
 * @return true: Successfully sent, false: error occurred
 */
bool NetworkManager::sendString(int socket, const std::string &message) {
    bool success = true;

    // Get message length in network byte order
    uint32_t len = message.size();
    len = htonl(len);

    // Send message length
    if (sendAll(socket, &len, sizeof len) != (sizeof len)) {
        success = false;
    }

    // Send the message
    if (success && sendAll(socket, message.data(), message.size()) != message.size()) {
        success = false;
    }

    return success;
}

size_t NetworkManager::recvAll(int socket, void *buff, size_t buffLen) {
    size_t result, total = 0;
    while (total < buffLen) {
        result = ::recv(socket, (void *) buff, buffLen, 0);
        if (result < 0) {
            return -1;
        } else if (result == 0) {
            break;
        }
        buff = static_cast<char *>(buff) + result;
        total += result;
    }
    return total;
}

/**
 * Receive a string at a socket.
 * @param socket
 * @return Received string. Error: empty string
 */
std::string NetworkManager::recvString(int socket) {
    // Receive message length in network byte order
    uint32_t len;
    if (recvAll(socket, &len, sizeof len) != (sizeof len)) {
        return "";
    }

    // Receive the message
    len = ntohl(len);
    std::string msg(len, '\0');
    if (recvAll(socket, &msg[0], len) != len) {
        return "";
    }

    // Return received string
    return msg;
}

#pragma endregion