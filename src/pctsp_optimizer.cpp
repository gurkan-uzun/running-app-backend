#include "pctsp_optimizer.h"
#include "dijkstra.h"
#include "nearest_neighbor.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cstdarg>

// Standalone logging function (replaces Flutter FFI-provided native_log)
void native_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

PctspOptimizer::PctspOptimizer(Graph& graph) : graph_(graph) {}

double PctspOptimizer::getDistance(int64_t from, int64_t to) {
    if (from == to) return 0.0;
    
    // Check if already computed in matrix
    // This is a simplified version - in practice we'd use proper indexing
    auto key = std::make_pair(from, to);
    
    // Use Dijkstra to find shortest path
    Dijkstra dijkstra;
    std::vector<int64_t> path = dijkstra.findShortestPath(graph_, from, to);
    
    if (path.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    
    // Calculate total distance along path
    double totalDist = 0.0;
    for (size_t i = 0; i < path.size() - 1; i++) {
        int64_t n1 = path[i];
        int64_t n2 = path[i + 1];
        
        // Find edge weight
        if (graph_.adjacency_list.find(n1) != graph_.adjacency_list.end()) {
            for (const auto& edge : graph_.adjacency_list[n1]) {
                if (edge.targetNodeId == n2) {
                    totalDist += edge.weight;
                    break;
                }
            }
        }
    }
    
    // Cache the path
    pathCache_[key] = path;
    
    return totalDist;
}

std::vector<int64_t> PctspOptimizer::getPath(int64_t from, int64_t to) {
    if (from == to) return {from};
    
    auto key = std::make_pair(from, to);
    
    // Check cache
    if (pathCache_.find(key) != pathCache_.end()) {
        return pathCache_[key];
    }
    
    // Compute path
    Dijkstra dijkstra;
    std::vector<int64_t> path = dijkstra.findShortestPath(graph_, from, to);
    pathCache_[key] = path;
    return path;
}

void PctspOptimizer::buildDistanceMatrix(int64_t startNodeId, const std::vector<OptPoi>& pois) {
    size_t n = pois.size() + 1; // start + POIs
    distMatrix_.resize(n, std::vector<double>(n, 0.0));
    
    nodeIds_.clear();
    nodeIds_.push_back(startNodeId);
    for (const auto& poi : pois) {
        nodeIds_.push_back(poi.nodeId);
    }
    
    native_log("PCTSP: Building distance matrix for %zu nodes", n);
    
    // Compute pairwise distances
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            double dist = getDistance(nodeIds_[i], nodeIds_[j]);
            distMatrix_[i][j] = dist;
            distMatrix_[j][i] = dist; // Symmetric for walking
        }
    }
    
    native_log("PCTSP: Distance matrix built");
}

double PctspOptimizer::calculateInsertionCost(
    const std::vector<int>& currentRoute,
    int newPoiIndex,
    int insertPosition
) {
    // currentRoute contains indices into distMatrix (0 = start, 1+ = POIs)
    // insertPosition is where to insert the new POI
    
    if (currentRoute.empty()) {
        // Route is just start -> start
        // Inserting POI: start -> POI -> start
        return distMatrix_[0][newPoiIndex] + distMatrix_[newPoiIndex][0];
    }
    
    if (insertPosition == 0) {
        // Insert at beginning: start -> NEW -> currentRoute[0] -> ...
        int next = currentRoute[0];
        double oldCost = distMatrix_[0][next];
        double newCost = distMatrix_[0][newPoiIndex] + distMatrix_[newPoiIndex][next];
        return newCost - oldCost;
    }
    
    if (insertPosition >= (int)currentRoute.size()) {
        // Insert at end: ... -> currentRoute.back() -> NEW -> start
        int prev = currentRoute.back();
        double oldCost = distMatrix_[prev][0];
        double newCost = distMatrix_[prev][newPoiIndex] + distMatrix_[newPoiIndex][0];
        return newCost - oldCost;
    }
    
    // Insert in middle
    int prev = currentRoute[insertPosition - 1];
    int next = currentRoute[insertPosition];
    double oldCost = distMatrix_[prev][next];
    double newCost = distMatrix_[prev][newPoiIndex] + distMatrix_[newPoiIndex][next];
    return newCost - oldCost;
}

std::pair<int, double> PctspOptimizer::findBestInsertion(
    const std::vector<int>& currentRoute,
    int newPoiIndex
) {
    int bestPos = 0;
    double bestCost = std::numeric_limits<double>::infinity();
    
    // Try all positions
    for (int pos = 0; pos <= (int)currentRoute.size(); pos++) {
        double cost = calculateInsertionCost(currentRoute, newPoiIndex, pos);
        if (cost < bestCost) {
            bestCost = cost;
            bestPos = pos;
        }
    }
    
    return {bestPos, bestCost};
}

OptimizedRoute PctspOptimizer::optimize(
    int64_t startNodeId,
    const std::vector<OptPoi>& pois,
    double targetDistance,
    double tolerance
) {
    OptimizedRoute result;
    result.totalDistance = 0.0;
    result.poiCount = 0;
    
    if (pois.empty()) {
        native_log("PCTSP: No POIs to optimize");
        result.nodeIds.push_back(startNodeId);
        return result;
    }
    
    native_log("PCTSP: Starting optimization with %zu POIs, target %.1fm", pois.size(), targetDistance);
    
    // Build distance matrix
    buildDistanceMatrix(startNodeId, pois);
    
    // Greedy insertion algorithm
    // currentRoute stores matrix indices of visited POIs (not including start/end)
    std::vector<int> currentRoute;
    std::vector<bool> visited(pois.size() + 1, false);
    visited[0] = true; // Start is always "visited"
    
    double currentDistance = 0.0;
    double maxDistance = targetDistance * (1.0 + tolerance);
    double minDistance = targetDistance * (1.0 - tolerance);
    
    // Sort POIs by prize/distance ratio (closest high-value first)
    std::vector<std::pair<double, int>> candidates;
    for (size_t i = 0; i < pois.size(); i++) {
        double distFromStart = distMatrix_[0][i + 1];
        if (distFromStart < std::numeric_limits<double>::infinity()) {
            // Ratio: higher prize and lower distance = better
            double ratio = pois[i].prize / (distFromStart + 1.0);
            candidates.push_back({ratio, (int)(i + 1)}); // i+1 is matrix index
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<std::pair<double, int>>());
    
    native_log("PCTSP: %zu reachable POI candidates", candidates.size());
    
    // Greedy insertion
    for (const auto& candidate : candidates) {
        int poiMatrixIdx = candidate.second;
        
        if (visited[poiMatrixIdx]) continue;
        
        // Find best position to insert this POI
        auto [bestPos, insertionCost] = findBestInsertion(currentRoute, poiMatrixIdx);
        
        // Check if we can afford this insertion
        double newDistance = currentDistance + insertionCost;
        
        // If adding this POI would exceed max distance, skip
        if (newDistance > maxDistance) {
            continue;
        }
        
        // Add POI to route
        currentRoute.insert(currentRoute.begin() + bestPos, poiMatrixIdx);
        currentDistance = newDistance;
        visited[poiMatrixIdx] = true;
        result.poiIndices.push_back(poiMatrixIdx - 1); // Convert back to POI index
        result.poiCount++;
        
        native_log("PCTSP: Added POI %d at pos %d, route distance now %.1fm", 
                   poiMatrixIdx - 1, bestPos, currentDistance);
        
        // If we've reached minimum target distance, we can stop
        if (currentDistance >= minDistance) {
            native_log("PCTSP: Reached minimum target distance");
            break;
        }
    }
    
    // Build full path from start through POIs back to start
    result.nodeIds.clear();
    
    // Start
    int64_t prevNode = startNodeId;
    result.nodeIds.push_back(prevNode);
    
    // Add path to each POI
    for (int matrixIdx : currentRoute) {
        int64_t nextNode = nodeIds_[matrixIdx];
        std::vector<int64_t> segment = getPath(prevNode, nextNode);
        
        // Add segment (skip first node as it's already in result)
        for (size_t i = 1; i < segment.size(); i++) {
            result.nodeIds.push_back(segment[i]);
        }
        prevNode = nextNode;
    }
    
    // Return to start
    if (prevNode != startNodeId) {
        std::vector<int64_t> returnSegment = getPath(prevNode, startNodeId);
        for (size_t i = 1; i < returnSegment.size(); i++) {
            result.nodeIds.push_back(returnSegment[i]);
        }
    }
    
    result.totalDistance = currentDistance;
    
    native_log("PCTSP: Optimization complete. Route has %zu nodes, visits %d POIs, distance %.1fm",
               result.nodeIds.size(), result.poiCount, result.totalDistance);
    
    return result;
}
