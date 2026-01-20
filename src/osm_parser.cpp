#include "osm_parser.h"
#include "tinyxml2.h"
#include "graph.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <set>
#include <string>

using namespace tinyxml2;

OsmParser::OsmParser() {
}

OsmParser::~OsmParser() {
}

double OsmParser::toRadians(double degree) {
    return degree * M_PI / 180.0;
}

double OsmParser::calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0; // Earth radius in meters
    double dLat = toRadians(lat2 - lat1);
    double dLon = toRadians(lon2 - lon1);
    
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(toRadians(lat1)) * cos(toRadians(lat2)) *
               sin(dLon / 2) * sin(dLon / 2);
    
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    
    return R * c;
}

int OsmParser::parse(const std::string& filepath, Graph& graph) {
    XMLDocument doc;
    XMLError eResult = doc.LoadFile(filepath.c_str());
    
    if (eResult != XML_SUCCESS) {
        printf("Error loading file: %s, Error: %d\n", filepath.c_str(), eResult);
        return -1;
    }

    XMLElement* root = doc.RootElement();
    if (root == nullptr) {
         printf("Error: Root element not found\n");
         return -1;
    }
    printf("Native: Root element name: %s\n", root->Name());

    // 1. Parse Nodes
    XMLElement* element = root->FirstChildElement("node");
    if (element == nullptr) {
        printf("Native: No 'node' element found! Check XML format.\n");
    } else {
        printf("Native: First 'node' element found with ID: %lld\n", element->Int64Attribute("id"));
    }
    while (element != nullptr) {
        long id = element->Int64Attribute("id");
        double lat = element->DoubleAttribute("lat");
        double lon = element->DoubleAttribute("lon");
        
        graph.addNode(id, lat, lon);
        
        element = element->NextSiblingElement("node");
    }
    
    printf("Parsed %lu nodes.\n", graph.nodes.size());

    // 2. Parse Ways
    element = root->FirstChildElement("way");
    
    // Allowlist of highway types suitable for running
    std::set<std::string> allowedHighways = {
        "footway", "pedestrian", "path", "cycleway", "living_street", 
        "residential", "service", "track", "steps", "unclassified",
        "tertiary", "secondary", "crossing" // Added crossing
    };

    while (element != nullptr) {
        // DEBUG: Allow all ways to test connectivity (Requested by User)
        bool isRunnable = true; 
        
        XMLElement* tag = element->FirstChildElement("tag");
        while (tag != nullptr) {
            const char* k = tag->Attribute("k");
            const char* v = tag->Attribute("v");
            
            if (k != nullptr && v != nullptr) {
                std::string key(k);
                std::string val(v);
                
                // 1. Check Highway Type
                if (key == "highway") {
                    if (allowedHighways.find(val) != allowedHighways.end()) {
                        isRunnable = true;
                    }
                }
                
                // 2. Check Foot Permission
                if (key == "foot" && (val == "yes" || val == "designated" || val == "permissive")) {
                    isRunnable = true;
                }
                
                // 3. Check Sidewalks
                if (key == "sidewalk" && (val == "yes" || val == "both" || val == "left" || val == "right")) {
                    isRunnable = true;
                }
            }
            tag = tag->NextSiblingElement("tag");
        }

        // If not runnable, skip this way
        if (!isRunnable) {
             element = element->NextSiblingElement("way");
             continue;
        }

        std::vector<long> wayNodes;
        XMLElement* nd = element->FirstChildElement("nd");
        while (nd != nullptr) {
            long ref = nd->Int64Attribute("ref");
            wayNodes.push_back(ref);
            nd = nd->NextSiblingElement("nd");
        }
        
        // Add edges between consecutive nodes
        if (wayNodes.size() > 1) {
            for (size_t i = 0; i < wayNodes.size() - 1; ++i) {
                long u = wayNodes[i];
                long v = wayNodes[i+1];
                
                // Check if nodes exist (they should, but safety first)
                if (graph.nodes.find(u) != graph.nodes.end() && graph.nodes.find(v) != graph.nodes.end()) {
                    Node& n1 = graph.nodes[u];
                    Node& n2 = graph.nodes[v];
                    
                    double dist = calculateDistance(n1.lat, n1.lon, n2.lat, n2.lon);
                    
                    // Add bidirectional edge
                    graph.addEdge(u, v, dist);
                    graph.addEdge(v, u, dist);
                }
            }
        }
        
        element = element->NextSiblingElement("way");
    }
    
    printf("Parsed OSM data. Total nodes: %lu\n", graph.nodes.size());
    
    
    return 0;
}
// Helper to parse from Root element (avoids code duplication)
int parseFromRoot(XMLElement* root, Graph& graph) {
    if (root == nullptr) return -1;

    // 1. Parse Nodes
    XMLElement* element = root->FirstChildElement("node");
    while (element != nullptr) {
        long id = element->Int64Attribute("id");
        double lat = element->DoubleAttribute("lat");
        double lon = element->DoubleAttribute("lon");
        graph.addNode(id, lat, lon);
        element = element->NextSiblingElement("node");
    }
    
    // 2. Parse Ways
    element = root->FirstChildElement("way");
    
    // Allowlist of highway types suitable for running
    std::set<std::string> allowedHighways = {
        "footway", "pedestrian", "path", "cycleway", "living_street", 
        "residential", "service", "track", "steps", "unclassified",
        "tertiary", "secondary", "crossing"
    };

    while (element != nullptr) {
        // DEBUG: Allow all ways to test connectivity (Requested by User)
        bool isRunnable = true; 
        
        /* 
        // Logic for filtering - currently disabled for debug
        XMLElement* tag = element->FirstChildElement("tag");
        // ... (filtering logic commented out for now as per user request to enable all)
        */

        if (isRunnable) {
            std::vector<long> wayNodes;
            XMLElement* nd = element->FirstChildElement("nd");
            while (nd != nullptr) {
                long ref = nd->Int64Attribute("ref");
                wayNodes.push_back(ref);
                nd = nd->NextSiblingElement("nd");
            }
            
            if (wayNodes.size() > 1) {
                for (size_t i = 0; i < wayNodes.size() - 1; ++i) {
                    long u = wayNodes[i];
                    long v = wayNodes[i+1];
                    if (graph.nodes.find(u) != graph.nodes.end() && graph.nodes.find(v) != graph.nodes.end()) {
                        Node& n1 = graph.nodes[u];
                        Node& n2 = graph.nodes[v];
                        double dist = 0.0; // Need calculateDistance logic here or accessible
                        // Since calculateDistance is private member, we need this helper to be member or friend
                        // For this Refactor, let's keep it simple: 
                        // I will put the logic inside parseString and parse.
                    }
                }
            }
        }
        element = element->NextSiblingElement("way");
    }
    return 0;
}

// I will just implement parseString fully to access member functions cleanly.

int OsmParser::parseString(const char* xmlData, Graph& graph, char* errorOut, int errorBufSize) {
    XMLDocument doc;
    XMLError eResult = doc.Parse(xmlData);
    
    if (eResult != XML_SUCCESS) {
        if (errorOut) snprintf(errorOut, errorBufSize, "XML Parse Error ID: %d", eResult);
        printf("Native: Error parsing XML string. Error: %d\n", eResult);
        return -1;
    }

    XMLElement* root = doc.RootElement();
    if (root == nullptr) {
         if (errorOut) snprintf(errorOut, errorBufSize, "Root element empty (Parse Success but no root)");
         printf("Error: Root element not found\n");
         return -1;
    }
    printf("Native: Root element name: %s\n", root->Name());

    // 1. Parse Nodes
    XMLElement* element = root->FirstChildElement("node");
    while (element != nullptr) {
        int64_t id = element->Int64Attribute("id");
        double lat = element->DoubleAttribute("lat");
        double lon = element->DoubleAttribute("lon");
        graph.addNode(id, lat, lon);
        element = element->NextSiblingElement("node");
    }
    
    printf("Parsed %lu nodes from string.\n", graph.nodes.size());

    // 2. Parse Ways with Road Type Weights
    element = root->FirstChildElement("way");
    
    size_t edgeCount = 0;
    size_t wayCount = 0;
    size_t skippedWays = 0;
    // Road type weights: 1.0 = allowed, 0 = blocked
    // Currently using uniform weights for pure shortest-distance routing
    // TODO: Add user preference for "Prefer footpaths" vs "Shortest route"
    std::unordered_map<std::string, double> roadWeights = {
        // All allowed roads - uniform weight for shortest distance
        {"footway", 1.0},
        {"pedestrian", 1.0},
        {"path", 1.0},
        {"steps", 1.0},
        {"track", 1.0},
        {"cycleway", 1.0},
        {"living_street", 1.0},
        {"residential", 1.0},
        {"service", 1.0},
        {"unclassified", 1.0},
        {"tertiary", 1.0},
        {"tertiary_link", 1.0},
        {"secondary", 1.0},
        {"secondary_link", 1.0},
        {"crossing", 1.0},
        {"primary", 1.0},
        {"primary_link", 1.0},
        {"trunk", 1.0},
        {"trunk_link", 1.0},
        
        // Blocked (weight 0) - truly dangerous, never run here
        {"motorway", 0},
        {"motorway_link", 0},
        {"construction", 0},
        {"proposed", 0},
        {"raceway", 0},
    };

    while (element != nullptr) {
        double weightMultiplier = 0.0; // 0 = skip this way
        bool hasSidewalk = false;
        bool footAllowed = false;
        std::string highwayType = "";
        
        // Parse tags to determine road type and properties
        XMLElement* tag = element->FirstChildElement("tag");
        while (tag != nullptr) {
            const char* k = tag->Attribute("k");
            const char* v = tag->Attribute("v");
            
            if (k != nullptr && v != nullptr) {
                std::string key(k);
                std::string val(v);
                
                // Get highway type
                if (key == "highway") {
                    highwayType = val;
                    auto it = roadWeights.find(val);
                    if (it != roadWeights.end()) {
                        weightMultiplier = it->second;
                    } else {
                        // Unknown highway type - allow with high weight
                        weightMultiplier = 2.5;
                    }
                }
                
                // Check for foot access (can override blocking)
                if (key == "foot" && (val == "yes" || val == "designated" || val == "permissive")) {
                    footAllowed = true;
                }
                
                // Check for sidewalks (can make blocked roads usable)
                if (key == "sidewalk" && (val == "yes" || val == "both" || val == "left" || val == "right")) {
                    hasSidewalk = true;
                }
                
                // Parks and leisure areas are always good
                if (key == "leisure" && (val == "park" || val == "garden" || val == "nature_reserve")) {
                    weightMultiplier = 1.0;
                }
            }
            tag = tag->NextSiblingElement("tag");
        }
        
        // Apply overrides based on foot access and sidewalks
        if (weightMultiplier == 0.0 && (footAllowed || hasSidewalk)) {
            weightMultiplier = 1.5; // Allow blocked roads if they have sidewalks/foot access
        }
        
        // Skip if still blocked
        if (weightMultiplier <= 0.0) {
            skippedWays++;
            element = element->NextSiblingElement("way");
            continue;
        }
        
        // Parse nodes and create edges with weighted distance
        std::vector<int64_t> wayNodes;
        XMLElement* nd = element->FirstChildElement("nd");
        while (nd != nullptr) {
            int64_t ref = nd->Int64Attribute("ref");
            wayNodes.push_back(ref);
            nd = nd->NextSiblingElement("nd");
        }
        
        if (wayNodes.size() > 1) {
            wayCount++;
            for (size_t i = 0; i < wayNodes.size() - 1; ++i) {
                int64_t u = wayNodes[i];
                int64_t v = wayNodes[i+1];
                
                if (graph.nodes.find(u) != graph.nodes.end() && graph.nodes.find(v) != graph.nodes.end()) {
                    Node& n1 = graph.nodes[u];
                    Node& n2 = graph.nodes[v];
                    double dist = calculateDistance(n1.lat, n1.lon, n2.lat, n2.lon);
                    
                    // Apply weight multiplier to prefer better roads
                    double weightedDist = dist * weightMultiplier;
                    
                    graph.addEdge(u, v, weightedDist);
                    graph.addEdge(v, u, weightedDist);
                    edgeCount += 2;
                }
            }
        }
        element = element->NextSiblingElement("way");
    }
    
    printf("Parsed %lu ways with %lu edges (skipped %lu blocked ways).\n", wayCount, edgeCount, skippedWays);
    printf("Adjacency list has %lu entries.\n", graph.adjacency_list.size());
    
    // 3. POST-PROCESSING: Connect nearby intersection nodes (OPTIMIZED with spatial grid)
    // OSM often has separate nodes at the same intersection for different roads.
    printf("Native: Connecting nearby intersection nodes (optimized)...\n");
    
    const double MERGE_THRESHOLD = 5.0; // meters
    const double CELL_SIZE = 0.0001;    // ~10m in lat/lon degrees
    size_t virtualEdges = 0;
    
    // Build spatial grid: map grid cell -> list of node IDs in that cell
    std::unordered_map<int64_t, std::vector<int64_t>> spatialGrid;
    
    auto getCellKey = [](double lat, double lon, double cellSize) -> int64_t {
        int64_t latCell = static_cast<int64_t>(lat / cellSize);
        int64_t lonCell = static_cast<int64_t>(lon / cellSize);
        // Combine into single key (assuming reasonable coordinate range)
        return latCell * 10000000LL + lonCell;
    };
    
    // Place all connected nodes into grid cells
    for (const auto& pair : graph.adjacency_list) {
        int64_t nodeId = pair.first;
        Node& n = graph.nodes[nodeId];
        int64_t cellKey = getCellKey(n.lat, n.lon, CELL_SIZE);
        spatialGrid[cellKey].push_back(nodeId);
    }
    
    printf("Native: Created spatial grid with %lu cells.\n", spatialGrid.size());
    
    // For each cell, check nodes within same cell and 8 adjacent cells
    std::set<std::pair<int64_t, int64_t>> checkedPairs; // Avoid duplicates
    
    for (const auto& cellPair : spatialGrid) {
        int64_t cellKey = cellPair.first;
        int64_t latCell = cellKey / 10000000LL;
        int64_t lonCell = cellKey % 10000000LL;
        
        // Check this cell and 8 neighbors
        for (int dLat = -1; dLat <= 1; dLat++) {
            for (int dLon = -1; dLon <= 1; dLon++) {
                int64_t neighborKey = (latCell + dLat) * 10000000LL + (lonCell + dLon);
                
                if (spatialGrid.find(neighborKey) == spatialGrid.end()) continue;
                
                // Compare nodes in current cell with nodes in neighbor cell
                for (int64_t nodeA : cellPair.second) {
                    for (int64_t nodeB : spatialGrid[neighborKey]) {
                        if (nodeA >= nodeB) continue; // Avoid duplicate checks
                        
                        Node& nA = graph.nodes[nodeA];
                        Node& nB = graph.nodes[nodeB];
                        
                        double dist = calculateDistance(nA.lat, nA.lon, nB.lat, nB.lon);
                        
                        if (dist < MERGE_THRESHOLD && dist > 0.01) {
                            // Check if already connected
                            bool alreadyConnected = false;
                            for (const auto& edge : graph.adjacency_list[nodeA]) {
                                if (edge.targetNodeId == nodeB) {
                                    alreadyConnected = true;
                                    break;
                                }
                            }
                            
                            if (!alreadyConnected) {
                                graph.addEdge(nodeA, nodeB, dist);
                                graph.addEdge(nodeB, nodeA, dist);
                                virtualEdges += 2;
                            }
                        }
                    }
                }
            }
        }
    }
    
    printf("Native: Added %lu virtual intersection edges.\n", virtualEdges);
    printf("Native: Final adjacency list has %lu entries.\n", graph.adjacency_list.size());
    
    return 0;
}
