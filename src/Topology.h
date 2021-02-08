#ifndef TOPOLOGY_H
#define TOPOLOGY_H

#include <nlohmann/json.hpp>
#include <list>
#include <vector>
#include <set>

using json = nlohmann::json;

class Topology {
public:
    // Topology Member to prevent the big Client class getting initialized all the time
    struct Peer {
        std::string hostname; // used as identification
        std::string nextHop; // next hop hostname
        std::set<std::string> neighbors; // host names of the neighbors of this peer

        // used for Dijkstra
        int distance;
        std::string previous;
    };

    explicit Topology(const std::string &centerPeer);

    // methods
    void addPeer(const std::string &hostname);
    int getPeerCount();
    void removePeer(const std::string &hostname);
    void setConnection(const std::string &hostname1, const std::string &hostname2, bool connected);
    bool isFractured() const;
    bool isUnderconnected() const;
    Peer *getPeer(const std::string &hostname);
    void plot();
    std::vector<std::string> getShortestPath(const std::string &hostname);
    std::map<std::string, std::string> getRoutingTable();
    void loadJson(const json &j);
    json toJson();
    std::vector<std::string> calculateBridgePeer();
    std::vector<std::string>
    calculateNewConnections(const std::set<std::string> &startingPeers = std::set<std::string>());
    std::string calculateNewUnderconnections();

private:
    // fields
    std::vector<Topology::Peer> peers;
    std::string centerPeer; // the hostname of the peer this Topology is running on

    // methods
    void calculateNextHops();
    static void sortByNeighborsAndName(std::vector<Peer> &sortPeers);
};


#endif
