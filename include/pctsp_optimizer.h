#ifndef PCTSP_OPTIMIZER_H
#define PCTSP_OPTIMIZER_H

#include <vector>
#include <cstdint>
#include <map>
#include <utility>
#include "graph.h"

// POI structure for optimization
struct OptPoi {
    int64_t nodeId;      // Snapped to nearest graph node
    double lat;
    double lon;
    double prize;        // Value/priority of visiting (1.0 = normal)
};

// Route result
struct OptimizedRoute {
    std::vector<int64_t> nodeIds;    // Full path through graph nodes
    std::vector<int> poiIndices;     // Which POIs are visited (indices into input array)
    double totalDistance;            // Total route distance in meters
    int poiCount;                    // Number of POIs visited
};

class PctspOptimizer {
public:
    PctspOptimizer(Graph& graph);
    
    // Main optimization function
    // Returns a circular route starting and ending at startNodeId
    // Tries to visit POIs within targetDistance budget
    OptimizedRoute optimize(
        int64_t startNodeId,
        const std::vector<OptPoi>& pois,
        double targetDistance,      // in meters
        double tolerance = 0.1      // 10% tolerance
    );

private:
    Graph& graph_;
    
    // Compute shortest path distance between two nodes (cached)
    double getDistance(int64_t from, int64_t to);
    
    // Build distance matrix for start + all POIs
    void buildDistanceMatrix(int64_t startNodeId, const std::vector<OptPoi>& pois);
    
    // Calculate insertion cost of adding a POI to current route
    double calculateInsertionCost(
        const std::vector<int>& currentRoute,
        int newPoiIndex,
        int insertPosition
    );
    
    // Find best position to insert a POI
    std::pair<int, double> findBestInsertion(
        const std::vector<int>& currentRoute,
        int newPoiIndex
    );
    
    // Get full path between two nodes using Dijkstra
    std::vector<int64_t> getPath(int64_t from, int64_t to);
    
    // Distance matrix: distMatrix[i][j] = distance from node i to node j
    // Index 0 = start, 1..n = POIs
    std::vector<std::vector<double>> distMatrix_;
    
    // Path cache for reconstructing full route
    std::map<std::pair<int64_t, int64_t>, std::vector<int64_t>> pathCache_;
    
    // Node IDs for matrix indexing
    std::vector<int64_t> nodeIds_; // [startNode, poi1Node, poi2Node, ...]
};

#endif // PCTSP_OPTIMIZER_H
