#include <netdb.h>
#include "Topology.h"
#include <graphviz/gvc.h>

#pragma region Constructor

Topology::Topology(const std::string &centerPeer) : centerPeer(centerPeer) {
    addPeer(centerPeer);
}

#pragma endregion

/**
 * Add a new peer identified by its hostname.
 * @param hostname of new peer
 */
void Topology::addPeer(const std::string &hostname) {
    Peer newPeer;
    newPeer.hostname = hostname;
    peers.push_back(newPeer);

    // trigger next hop calculation on change
    calculateNextHops();
}

/**
 * Remove a peer from the topology.
 * @param hostname Hostname of the peer
 */
void Topology::removePeer(const std::string &hostname) {
    auto currentPeerIt = std::find_if(peers.begin(), peers.end(),
                                      [&hostname](const Peer &peer) { return peer.hostname == hostname; });
    if (currentPeerIt == peers.end()) return;
    auto currentPeer = currentPeerIt.base();

    // remove peer from neighbors
    for (const auto &neighbor: currentPeer->neighbors) {
        auto currentNeighbor = getPeer(neighbor);
        if (currentNeighbor == nullptr) continue;

        currentNeighbor->neighbors.erase(currentPeer->hostname);
    }

    // remove peer from peers
    peers.erase(currentPeerIt);

    // trigger next hop calculation on change
    calculateNextHops();
}

/**
 * Set whether two peers are connected.
 * @param hostname1 First peer
 * @param hostname2 Second peer
 * @param connected
 */
void Topology::setConnection(const std::string &hostname1, const std::string &hostname2, bool connected) {
    auto peer1 = getPeer(hostname1);
    auto peer2 = getPeer(hostname2);
    if (peer1 == nullptr || peer2 == nullptr) return;

    if (connected) {
        peer1->neighbors.insert(hostname2);
        peer2->neighbors.insert(hostname1);
    } else {
        peer1->neighbors.erase(hostname2);
        peer2->neighbors.erase(hostname1);
    }

    // trigger next hop calculation on change
    calculateNextHops();
}

/**
 * Get a pointer to a peer. This pointer should not be stored, because it can get invalid!
 * @param hostname of the peer
 * @return Pointer to the peer of nullptr if not found.
 */
Topology::Peer *Topology::getPeer(const std::string &hostname) {
    auto currentPeerIt = std::find_if(peers.begin(), peers.end(),
                                      [&hostname](const Peer &peer) { return peer.hostname == hostname; });
    if (currentPeerIt == peers.end()) return nullptr;
    return currentPeerIt.base();
}

/**
 * Get the number of current peers.
 */
int Topology::getPeerCount() {
    return peers.size();
}

/**
 * Plot the topology as a graph.
 */
void Topology::plot() {
    GVC_t *gvc = gvContext();

    auto g = agopen(nullptr, Agundirected, nullptr);

    std::map<std::string, Agnode_s *> nodes;
    // add all nodes
    for (const auto &peer: peers) {
        nodes.emplace(peer.hostname, agnode(g, (char *) peer.hostname.c_str(), 1));
    }

    // add edges
    std::set<std::string> processed;
    for (const auto &peer: peers) {
        processed.insert(peer.hostname);
        for (const auto &neighbor: peer.neighbors) {
            if (processed.find(neighbor) != processed.end()) continue;
            agedge(g, nodes.find(peer.hostname)->second, nodes.find(neighbor)->second, nullptr, 1);
        }
    }

    gvLayout(gvc, g, "dot");
    auto fp = fopen("plot.png", "w");
    gvRender(gvc, g, "png", fp);

    gvFreeLayout(gvc, g);
    agclose(g);
    fclose(fp);
}

/**
 * Get shortest path to the peer.
 * @param hostname of the target peer.
 * @return vector of peer hostnames on shortest path
 */
std::vector<std::string> Topology::getShortestPath(const std::string &hostname) {
    auto peer = getPeer(hostname);
    std::vector<std::string> path;
    // insert end of path first
    path.push_back(hostname);

    // if hostname is unknown, center peer is passed or peer is unreachable
    if (peer == nullptr || peer->hostname == centerPeer || peer->previous.empty()) {
        return path;
    }

    // if peer is not the center or a neighbor of the center
    if (!peer->previous.empty() && peer->previous != centerPeer) {
        auto previous = getPeer(peer->previous);
        while (previous != nullptr && previous->hostname != centerPeer) {
            path.push_back(previous->hostname);
            previous = getPeer(previous->previous);
        }
    }

    // insert the start of path last
    path.push_back(centerPeer);

    // because we build the path in reverse
    std::reverse(path.begin(), path.end());

    return path;
}

std::map<std::string, std::string> Topology::getRoutingTable() {
    std::map<std::string, std::string> routingTable;
    for (auto const &peer : peers) {
        routingTable.emplace(peer.hostname, peer.nextHop);
    }
    return routingTable;
}

/**
 * Load a topology from a json object
 * @param j
 */
void Topology::loadJson(const json &j) {
    // clear the peers and re-add the center
    peers.clear();
    addPeer(centerPeer);

    // neighbor pairs
    std::vector<std::array<std::string, 2>> connections;
    for (auto &item : j.items()) {
        std::string hostname = item.value().value("hostname", "");
        std::vector<std::string> neighbors = item.value().value("neighbors", std::vector<std::string>());
        if (hostname.empty()) continue;

        addPeer(hostname);
        for (const auto &neighbor: neighbors) {
            connections.push_back({hostname, neighbor});
        }
    }

    for (const auto &connection: connections) {
        setConnection(connection[0], connection[1], true);
    }
}

/**
 * Convert topology to json
 * @return json object of topology
 */
json Topology::toJson() {
    json j;
    for (const auto &peer: peers) {
        j.push_back({{"hostname",  peer.hostname},
                     {"neighbors", peer.neighbors}});
    }
    return j;
}

/**
 * Calculate next hops for all peers.
 * To send a message to a peer, send the message to peer.nextHop.
 * peer.nextHop contains the hostname of the peer that is directly connected to the center peer.
 * If a peer is unreachable, the nextHop and previous will be empty.
 * @param start The hostname of the start peer
 */
void Topology::calculateNextHops() {
    std::set<std::string> Q;
    // reset
    for (auto &peer: peers) {
        peer.distance = peer.hostname == centerPeer ? 0 : INT32_MAX - 1;
        peer.previous = "";
        Q.insert(peer.hostname);
    }

    while (!Q.empty()) {
        // Peer in Q with minimum distance
        Peer *u;
        int minDistance = INT32_MAX - 1;
        for (const auto &hostname : Q) {
            auto peer = getPeer(hostname);
            if (peer->distance <= minDistance) {
                u = peer;
                minDistance = peer->distance;
            }
        }

        // Remove u from Q
        Q.erase(u->hostname);

        for (const auto &neighborHostname : u->neighbors) {
            auto neighbor = getPeer(neighborHostname);
            if (u->distance + 1 < neighbor->distance) {
                neighbor->distance = u->distance + 1;
                neighbor->previous = u->hostname;
            }
        }
    }

    // update next hops for each peer
    for (auto &peer: peers) {
        // unreachable nodes
        if (peer.distance == INT32_MAX - 1) {
            peer.previous = "";
            peer.nextHop = "";
            continue;
        }
        // if start node or neighbor
        if (peer.previous.empty() || peer.previous == centerPeer) {
            peer.nextHop = peer.hostname;
            continue;
        }

        auto previous = getPeer(peer.previous);
        while (previous != nullptr && previous->previous != centerPeer) {
            previous = getPeer(previous->previous);
        }
        peer.nextHop = previous != nullptr ? previous->hostname : "";
    }
}

/**
 * Calculate the peers that should connect to a new peer.
 * @return Vector of one or two hostnames that should connect
 */
std::vector<std::string> Topology::calculateBridgePeer() {
    std::vector<std::string> bridgePeers;
    if (peers.empty()) return bridgePeers;

    auto peersCopy = peers;
    // sort by count of neighbors and hostname
    sortByNeighborsAndName(peersCopy);

    bridgePeers.push_back(peersCopy.at(0).hostname);

    // when a fifth peer connects, we need two connections
    if (peers.size() >= 4) bridgePeers.push_back(peersCopy.at(1).hostname);

    return bridgePeers;
}

/**
 * Check if the network is fractured into parts.
 * @return true: a unreachable peer exists
 */
bool Topology::isFractured() const {
    return std::any_of(peers.begin(), peers.end(), [](const Peer &peer) { return peer.nextHop.empty(); });
}

/**
 * Calculate the hostnames to which the center peer should connect after a fracture happens.
 * @param startingPeers used for recursive calls to stay in the same reachable peers
 * @return vector of the hostnames
 */
std::vector<std::string> Topology::calculateNewConnections(const std::set<std::string> &startingPeers) {
    std::vector<std::string> newConnectionTargets;

    // get the reachable nodes
    std::vector<Peer> reachablePeers;
    std::vector<Peer> unreachablePeers;
    for (const auto &peer : peers) {
        if (peer.nextHop.empty()) {
            unreachablePeers.push_back(peer);
        } else if (startingPeers.empty()) {
            reachablePeers.push_back(peer);
        } else if (startingPeers.find(peer.hostname) != startingPeers.end()) {
            reachablePeers.push_back(peer);
        }
    }

    auto peersCopy = peers;
    // sort by hostname
    std::sort(peersCopy.begin(), peersCopy.end(),
              [](const Peer &peer1, const Peer &peer2) { return peer1.hostname < peer2.hostname; });

    // group with the lowest peer connected builds new connections
    auto currentPeerIt = std::find_if(reachablePeers.begin(), reachablePeers.end(),
                                      [&peersCopy](const Peer &peer) {
                                          return peer.hostname == peersCopy.at(0).hostname;
                                      });
    if (currentPeerIt == reachablePeers.end()) return newConnectionTargets;

    // sort reachable and unreachable peers by hostname and count of neighbor
    sortByNeighborsAndName(reachablePeers);
    sortByNeighborsAndName(unreachablePeers);

    auto firstReachablePeer = reachablePeers.at(0).hostname;
    auto firstUnreachablePeer = unreachablePeers.at(0).hostname;
    // try a new connection between the first reachable peer and the first unreachable
    setConnection(firstReachablePeer, firstUnreachablePeer, true);
    if (centerPeer == firstReachablePeer) {
        newConnectionTargets.push_back(firstUnreachablePeer);
    }
    // check if it is still fractured -> this happens if the network is fractured into three groups
    if (isFractured()) {
        // recursive call
        std::set<std::string> reachableHostnames;
        for (const auto &peer : reachablePeers) {
            reachableHostnames.insert(peer.hostname);
        }
        auto secondsNewConnections = calculateNewConnections(reachableHostnames);
        newConnectionTargets.insert(newConnectionTargets.end(), secondsNewConnections.begin(),
                                    secondsNewConnections.end());
    }

    // reset the connection
    setConnection(firstReachablePeer, firstUnreachablePeer, false);

    return newConnectionTargets;
}

/**
 * When we have 5 or more peers, we need 2 connections per peer.
 * @return true: min. 1 peer has only 1 connection
 */
bool Topology::isUnderconnected() const {
    if (peers.size() < 5) return false;
    return std::any_of(peers.begin(), peers.end(), [](const Peer &peer) { return peer.neighbors.size() == 1; });
}

/**
 * Calculate the hostname to which the center peer should connect after a underconnection happens.
 * @return hostname
 */
std::string Topology::calculateNewUnderconnections() {
    auto peersCopy = peers;
    sortByNeighborsAndName(peersCopy);

    // second peer should connect to the first one
    if (peersCopy.at(1).hostname == centerPeer) return peersCopy.at(0).hostname;
    return "";
}

/**
 * Sort the passed vector ascending by the count of neighbors and the hostname.
 * @param sortPeers
 */
void Topology::sortByNeighborsAndName(std::vector<Peer> &sortPeers) {
    std::sort(sortPeers.begin(), sortPeers.end(),
              [](const Peer &peer1, const Peer &peer2) {
                  auto size1 = peer1.neighbors.size();
                  auto size2 = peer2.neighbors.size();
                  if (size1 < size2) {
                      return true;
                  }
                  if (size1 == size2) {
                      return peer1.hostname < peer2.hostname;
                  }
                  return false;
              });
}