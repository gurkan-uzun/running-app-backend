#ifndef GRAPH_H
#define GRAPH_H

#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint> // For int64_t

struct Node {
    int64_t id;
    double lat;
    double lon;
    // Tags can be added here
};

struct Edge {
    int64_t targetNodeId;
    double weight; // Distance in meters
};

class Graph {
public:
    std::unordered_map<int64_t, Node> nodes;
    std::unordered_map<int64_t, std::vector<Edge>> adjacency_list;

    void addNode(int64_t id, double lat, double lon) {
        nodes[id] = {id, lat, lon};
    }

    void addEdge(int64_t u, int64_t v, double weight) {
        adjacency_list[u].push_back({v, weight});
    }
};

#endif // GRAPH_H
