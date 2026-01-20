#include "dijkstra.h"
#include <queue>
#include <algorithm>
#include <map>

// Helper struct is now in header

std::vector<int64_t> Dijkstra::findShortestPath(Graph& graph, int64_t startNodeId, int64_t endNodeId) {
    std::unordered_map<int64_t, double> dist;
    std::unordered_map<int64_t, int64_t> parent;
    std::priority_queue<State, std::vector<State>, std::greater<State>> pq;
    
    // Initialize distances
    for (const auto& pair : graph.nodes) {
        dist[pair.first] = std::numeric_limits<double>::infinity();
    }
    
    dist[startNodeId] = 0.0;
    pq.push({startNodeId, 0.0});
    
    int visitedCount = 0;
    while (!pq.empty()) {
        State current = pq.top();
        pq.pop();
        
        int64_t u = current.nodeId;
        double d = current.cost;
        
        visitedCount++;
        // Use native_log if possible, otherwise printf? Dijkstra doesn't include running_app.cpp functions.
        // But running_app.cpp uses native_log. 
        // For now, let's keep printf as backup, or remove it if not needed.
        // Actually, let's use printf, but running_app.cpp routes stdout to console? No.
        // Logging inside Dijkstra is hard without dependency injection.
        // Let's rely on running_app.cpp logging for now.
        // I will keep the existing printf for now, user might see it in console if not redirected.
        // Wait, I should update printf logic if type changed? 
        // Nope, printf uses %d or %ld. int64_t needs %lld.
        if (visitedCount % 1000 == 0) printf("Native: Dijkstra Visited %d nodes...\n", visitedCount);
        
        if (d > dist[u]) continue;
        if (u == endNodeId) {
             printf("Native: Target found! Cost: %.2f\n", d);
             break; // Found target
        }
        
        if (graph.adjacency_list.find(u) != graph.adjacency_list.end()) {
            for (const auto& edge : graph.adjacency_list[u]) {
                int64_t v = edge.targetNodeId;
                double weight = edge.weight;
                
                if (dist[u] + weight < dist[v]) {
                    dist[v] = dist[u] + weight;
                    parent[v] = u;
                    pq.push({v, dist[v]});
                }
            }
        }
    }
    
    // Reconstruct path
    std::vector<int64_t> path;
    if (dist[endNodeId] == std::numeric_limits<double>::infinity()) {
        return path; // No path found
    }
    
    int64_t curr = endNodeId;
    while (curr != startNodeId) {
        path.push_back(curr);
        curr = parent[curr];
    }
    path.push_back(startNodeId);
    std::reverse(path.begin(), path.end());
    
    return path;
}
