#include "httplib.h"
#include "json.hpp"
#include "graph.h"
#include "osm_parser.h"
#include "dijkstra.h"
#include "nearest_neighbor.h"
#include "pctsp_optimizer.h"
#include "poi.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include "tinyxml2.h"

using json = nlohmann::json;

// Global state
Graph g_graph;
std::vector<Poi> g_pois;
bool g_isGraphLoaded = false;

// Helper: Load OSM file and build graph
bool loadOsmFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filepath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xmlContent = buffer.str();
    file.close();
    
    g_graph = Graph();
    OsmParser parser;
    char errorBuffer[256] = {0};
    int result = parser.parseString(xmlContent.c_str(), g_graph, errorBuffer, 256);
    
    if (result == 0) {
        g_isGraphLoaded = true;
        std::cout << "Graph loaded: " << g_graph.nodes.size() << " nodes" << std::endl;
        return true;
    } else {
        std::cerr << "Parse error: " << errorBuffer << std::endl;
        return false;
    }
}

int main(int argc, char* argv[]) {
    httplib::Server svr;
    
    // Request logging middleware
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        auto now = std::time(nullptr);
        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::cout << "[" << timeStr << "] " 
                  << req.method << " " << req.path 
                  << " -> " << res.status << std::endl;
    });
    
    // Default OSM file path
    std::string osmFilePath = "data/istanbul.osm";
    if (argc > 1) {
        osmFilePath = argv[1];
    }
    
    // Try to load OSM data at startup
    std::cout << "Loading OSM data from: " << osmFilePath << std::endl;
    if (loadOsmFile(osmFilePath)) {
        std::cout << "Graph loaded successfully!" << std::endl;
    } else {
        std::cout << "Warning: Could not load OSM file. Use /api/init-graph to load." << std::endl;
    }

    // Health check
    svr.Get("/api/health", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        response["status"] = "ok";
        response["graph_loaded"] = g_isGraphLoaded;
        response["nodes_count"] = g_isGraphLoaded ? g_graph.nodes.size() : 0;
        res.set_content(response.dump(), "application/json");
    });

    // Initialize graph from OSM XML
    svr.Post("/api/init-graph", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string osmData = body["osm_data"];
            
            g_graph = Graph();
            g_pois.clear();
            OsmParser parser;
            char errorBuffer[256] = {0};
            int result = parser.parseString(osmData.c_str(), g_graph, errorBuffer, 256);
            
            // Also parse POIs from the OSM data
            // Using tinyxml2 directly (already included via osm_parser)
            tinyxml2::XMLDocument doc;
            if (doc.Parse(osmData.c_str()) == tinyxml2::XML_SUCCESS) {
                tinyxml2::XMLElement* root = doc.RootElement();
                if (root) {
                    tinyxml2::XMLElement* node = root->FirstChildElement("node");
                    while (node) {
                        // Check if this node has POI-relevant tags
                        bool hasPoi = false;
                        std::string name = "Unknown";
                        std::string category = "other";
                        std::string rawCategory = "";
                        
                        tinyxml2::XMLElement* tag = node->FirstChildElement("tag");
                        while (tag) {
                            const char* k = tag->Attribute("k");
                            const char* v = tag->Attribute("v");
                            if (k && v) {
                                std::string key(k);
                                std::string val(v);
                                
                                if (key == "name") {
                                    name = val;
                                }
                                
                                // Map OSM tags to categories
                                if (key == "amenity") {
                                    hasPoi = true;
                                    rawCategory = val;
                                    if (val == "cafe" || val == "coffee_shop" || val == "ice_cream") category = "cafe";
                                    else if (val == "restaurant" || val == "fast_food" || val == "food_court") category = "restaurant";
                                    else category = "amenity";
                                } else if (key == "tourism") {
                                    hasPoi = true;
                                    rawCategory = val;
                                    if (val == "museum" || val == "gallery" || val == "arts_centre") category = "museum";
                                    else if (val == "viewpoint" || val == "observation_tower") category = "viewpoint";
                                    else if (val == "attraction") category = "monument";
                                    else category = "tourism";
                                } else if (key == "leisure") {
                                    hasPoi = true;
                                    rawCategory = val;
                                    if (val == "park" || val == "garden" || val == "playground") category = "park";
                                    else category = "nature";
                                } else if (key == "natural") {
                                    hasPoi = true;
                                    rawCategory = val;
                                    if (val == "beach") category = "beach";
                                    else category = "nature";
                                } else if (key == "historic") {
                                    hasPoi = true;
                                    rawCategory = val;
                                    category = "monument";
                                }
                            }
                            tag = tag->NextSiblingElement("tag");
                        }
                        
                        if (hasPoi) {
                            Poi poi;
                            poi.id = node->Int64Attribute("id");
                            poi.lat = node->DoubleAttribute("lat");
                            poi.lon = node->DoubleAttribute("lon");
                            poi.name = name;
                            poi.category = category;
                            poi.rawCategory = rawCategory;
                            g_pois.push_back(poi);
                        }
                        
                        node = node->NextSiblingElement("node");
                    }
                }
            }
            std::cout << "Parsed " << g_pois.size() << " POIs from OSM data." << std::endl;
            
            json response;
            if (result == 0) {
                g_isGraphLoaded = true;
                response["success"] = true;
                response["nodes_count"] = g_graph.nodes.size();
                response["pois_count"] = g_pois.size();
            } else {
                response["success"] = false;
                response["error"] = std::string(errorBuffer);
            }
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            json response;
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Simple A-to-B routing
    svr.Post("/api/get-route", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        
        if (!g_isGraphLoaded) {
            response["success"] = false;
            response["error"] = "Graph not loaded";
            res.status = 400;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        try {
            auto body = json::parse(req.body);
            double startLat = body["start"]["lat"];
            double startLon = body["start"]["lon"];
            double endLat = body["end"]["lat"];
            double endLon = body["end"]["lon"];
            
            // Find nearest nodes
            int64_t startNode = NearestNeighbor::findNearestNode(g_graph, startLat, startLon);
            int64_t endNode = NearestNeighbor::findNearestNode(g_graph, endLat, endLon);
            
            if (startNode == -1 || endNode == -1) {
                response["success"] = false;
                response["error"] = "Could not find nearest nodes";
                res.status = 400;
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            // Run Dijkstra
            Dijkstra dijkstra;
            std::vector<int64_t> path = dijkstra.findShortestPath(g_graph, startNode, endNode);
            
            if (path.empty()) {
                response["success"] = false;
                response["error"] = "No path found";
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            // Build route response
            json route = json::array();
            double totalDistance = 0;
            
            for (size_t i = 0; i < path.size(); i++) {
                Node& n = g_graph.nodes[path[i]];
                route.push_back({{"lat", n.lat}, {"lon", n.lon}});
                
                if (i > 0) {
                    for (const auto& edge : g_graph.adjacency_list[path[i-1]]) {
                        if (edge.targetNodeId == path[i]) {
                            totalDistance += edge.weight;
                            break;
                        }
                    }
                }
            }
            
            response["success"] = true;
            response["route"] = route;
            response["distance_meters"] = totalDistance;
            response["points_count"] = path.size();
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Multi-POI circular route optimization
    svr.Post("/api/optimize-route", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        
        if (!g_isGraphLoaded) {
            response["success"] = false;
            response["error"] = "Graph not loaded";
            res.status = 400;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        try {
            auto body = json::parse(req.body);
            double startLat = body["start"]["lat"];
            double startLon = body["start"]["lon"];
            double targetDistance = body["target_distance_meters"];
            
            int64_t startNode = NearestNeighbor::findNearestNode(g_graph, startLat, startLon);
            if (startNode == -1) {
                response["success"] = false;
                response["error"] = "Could not find start node";
                res.status = 400;
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            std::vector<OptPoi> pois;
            for (const auto& poi : body["pois"]) {
                double lat = poi["lat"];
                double lon = poi["lon"];
                int64_t nodeId = NearestNeighbor::findNearestNode(g_graph, lat, lon);
                if (nodeId != -1) {
                    OptPoi p;
                    p.nodeId = nodeId;
                    p.lat = lat;
                    p.lon = lon;
                    p.prize = poi.value("prize", 1.0);
                    pois.push_back(p);
                }
            }
            
            if (pois.empty()) {
                response["success"] = false;
                response["error"] = "No valid POIs found";
                res.status = 400;
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            PctspOptimizer optimizer(g_graph);
            OptimizedRoute result = optimizer.optimize(startNode, pois, targetDistance);
            
            if (result.nodeIds.empty()) {
                response["success"] = false;
                response["error"] = "Could not generate route";
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            json route = json::array();
            for (int64_t nodeId : result.nodeIds) {
                if (g_graph.nodes.find(nodeId) != g_graph.nodes.end()) {
                    Node& n = g_graph.nodes[nodeId];
                    route.push_back({{"lat", n.lat}, {"lon", n.lon}});
                }
            }
            
            response["success"] = true;
            response["route"] = route;
            response["total_distance_meters"] = result.totalDistance;
            response["pois_visited"] = result.poiCount;
            response["points_count"] = result.nodeIds.size();
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Get nearest walkable node (single point)
    svr.Post("/api/nearest-node", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        
        if (!g_isGraphLoaded) {
            response["success"] = false;
            response["error"] = "Graph not loaded";
            res.status = 400;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        try {
            auto body = json::parse(req.body);
            double lat = body["lat"];
            double lon = body["lon"];
            
            int64_t nodeId = NearestNeighbor::findNearestNode(g_graph, lat, lon);
            
            if (nodeId == -1) {
                response["success"] = false;
                response["error"] = "No node found";
                res.set_content(response.dump(), "application/json");
                return;
            }
            
            Node& n = g_graph.nodes[nodeId];
            response["success"] = true;
            response["lat"] = n.lat;
            response["lon"] = n.lon;
            response["node_id"] = nodeId;
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // BATCH: Get nearest walkable nodes for multiple points (reduces N calls to 1)
    svr.Post("/api/nearest-nodes", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        
        if (!g_isGraphLoaded) {
            response["success"] = false;
            response["error"] = "Graph not loaded";
            res.status = 400;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        try {
            auto body = json::parse(req.body);
            json results = json::array();
            
            // Expect {"points": [{"lat": x, "lon": y}, ...]}
            for (const auto& point : body["points"]) {
                double lat = point["lat"];
                double lon = point["lon"];
                
                int64_t nodeId = NearestNeighbor::findNearestNode(g_graph, lat, lon);
                
                json nodeResult;
                if (nodeId != -1) {
                    Node& n = g_graph.nodes[nodeId];
                    nodeResult["success"] = true;
                    nodeResult["lat"] = n.lat;
                    nodeResult["lon"] = n.lon;
                    nodeResult["node_id"] = nodeId;
                } else {
                    nodeResult["success"] = false;
                    nodeResult["lat"] = lat;  // Return original coords
                    nodeResult["lon"] = lon;
                }
                results.push_back(nodeResult);
            }
            
            response["success"] = true;
            response["results"] = results;
            response["count"] = results.size();
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Get POIs within radius of a center point
    svr.Post("/api/pois", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        
        if (!g_isGraphLoaded) {
            response["success"] = false;
            response["error"] = "Graph not loaded - POIs are parsed during init-graph";
            res.status = 400;
            res.set_content(response.dump(), "application/json");
            return;
        }
        
        try {
            auto body = json::parse(req.body);
            double centerLat = body["lat"];
            double centerLon = body["lon"];
            double radius = body.value("radius", 1000.0); // Default 1km
            
            // Haversine distance calculation (reuse from osm_parser)
            auto haversine = [](double lat1, double lon1, double lat2, double lon2) -> double {
                const double R = 6371000.0; // Earth radius in meters
                double dLat = (lat2 - lat1) * M_PI / 180.0;
                double dLon = (lon2 - lon1) * M_PI / 180.0;
                double a = sin(dLat / 2) * sin(dLat / 2) +
                           cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                           sin(dLon / 2) * sin(dLon / 2);
                double c = 2 * atan2(sqrt(a), sqrt(1 - a));
                return R * c;
            };
            
            json pois = json::array();
            for (const auto& poi : g_pois) {
                double dist = haversine(centerLat, centerLon, poi.lat, poi.lon);
                if (dist <= radius) {
                    pois.push_back({
                        {"id", poi.id},
                        {"lat", poi.lat},
                        {"lon", poi.lon},
                        {"name", poi.name},
                        {"category", poi.category},
                        {"rawCategory", poi.rawCategory},
                        {"distance", dist}
                    });
                }
            }
            
            response["success"] = true;
            response["pois"] = pois;
            response["count"] = pois.size();
            response["total_pois"] = g_pois.size();
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    std::cout << "Starting server on http://localhost:8080" << std::endl;
    svr.listen("0.0.0.0", 8080);
    
    return 0;
}