#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "graph.h"
#include <vector>
#include <limits>

#include <cstdint>

struct State {
    int64_t nodeId;
    double cost;
    
    bool operator>(const State& other) const {
        return cost > other.cost;
    }
};

class Dijkstra {
public:
    // Returns a vector of node IDs representing the shortest path
    std::vector<int64_t> findShortestPath(Graph& graph, int64_t startNodeId, int64_t endNodeId);
};

#endif // DIJKSTRA_H
