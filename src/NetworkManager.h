#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <poll.h>
#include "Logger.h"
#include "IpManager.h"
#include <nlohmann/json.hpp>
#include <set>

using json = nlohmann::json;

class NetworkManager {
public:
    explicit NetworkManager(int multicastPort, int peerPort);

    // getter
    const std::string &getHostname() const { return localHostname; }
    const std::string &getIp() const { return ip; }

    // methods
    void createMulticastSocket();
    json processMulticastSocket();
    json processPeerSockets();
    std::string connectToPeer(const std::string &peerIp, std::string port = "");
    void createPeerPollSocket();
    void sendDiscoveryMessage() const;
    json sendCommand(Type type, const json &payload, const std::set<std::string> &nextHops);
    void forwardMessage(const json &message, const std::set<std::string> &nextHops);
    bool acceptPeerConnection(int timeout = 2);
    void closeAllSockets();
    std::set<std::string> getNeighbors();

private:
    // fields
    const std::string multicastAddr = "ff12::1234";
    uint16_t multicastPort;
    uint16_t peerPort;
    Logger &logger;
    pollfd *multicastPollSocket{};
    pollfd peerPollSockets[4]{}; // place for the poll socket and 3 connections
    int peerSocketsCount = 0;
    std::map<std::string, int> hostnameSockets;
    IpManager ips;
    std::map<std::string, int> hostnamePort;
    std::string localHostname;
    std::string ip;
    int messageId = 0;

    // methods
    json buildJson(bool proposal, Type type, const json &payload);
    void addToPeerPollSockets(int socket);
    void removeFromPeerPollSockets(int socket);
    std::string reverseLookup(int socket);
    std::string getLocalHostname();
    std::string getLocalIPv6();
    int getSocket(std::string &hostname) const;

    //statics
    static bool sendString(int socket, const std::string &message);
    static size_t sendAll(int socket, void const *buff, size_t buffLen);
    static std::string recvString(int socket);
    static size_t recvAll(int socket, void *buff, size_t buffLen);
};

#endif
