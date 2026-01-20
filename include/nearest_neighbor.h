#ifndef NEAREST_NEIGHBOR_H
#define NEAREST_NEIGHBOR_H

#include "graph.h"
#include <cmath>
#include <limits>

#include <cstdint>

class NearestNeighbor {
public:
    static int64_t findNearestNode(const Graph& graph, double lat, double lon) {
        int64_t bestNodeId = -1;
        double minDistance = std::numeric_limits<double>::max();

        // Simple linear search for now. 
        // OPTIMIZATION: Use a spatial index (QuadTree/K-d Tree) for large graphs.
        for (const auto& pair : graph.nodes) {
            const Node& node = pair.second;
            
            // CRITICAL FIX: Only snap to nodes that are actually part of the road network (have edges)
            if (graph.adjacency_list.find(node.id) == graph.adjacency_list.end()) {
                continue;
            }
            
            double dist = calculateSquaredDistance(lat, lon, node.lat, node.lon);
            
            if (dist < minDistance) {
                minDistance = dist;
                bestNodeId = node.id;
            }
        }
        return bestNodeId;
    }

private:
    static double calculateSquaredDistance(double lat1, double lon1, double lat2, double lon2) {
        double dLat = lat1 - lat2;
        double dLon = lon1 - lon2;
        return dLat * dLat + dLon * dLon;
    }
};

#endif // NEAREST_NEIGHBOR_H
