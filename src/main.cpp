#include "httplib.h"
#include "json.hpp"
#include "graph.h"
#include "osm_parser.h"
#include "dijkstra.h"
#include "nearest_neighbor.h"
#include "pctsp_optimizer.h"
#include "poi.h"
#include "region_cache.h"
#include "tile_system.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>
#include "tinyxml2.h"

using json = nlohmann::json;

// Global cache manager (replaces old g_graph / g_pois / g_isGraphLoaded)
RegionCache g_cache("cache", 7200); // 2-hour L1 TTL

// Helper: Get the graph for a given lat/lon via the cache.
// Returns nullptr if no region could be loaded.
CachedRegion* getRegionForPoint(double lat, double lon) {
    return g_cache.getRegion(lat, lon);
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

    // Default OSM file path for pre-seeding
    std::string osmFilePath = "data/istanbul.osm";
    if (argc > 1) {
        osmFilePath = argv[1];
    }

    // Try to pre-load OSM data at startup (backward compatible)
    std::cout << "Attempting to pre-load OSM data from: " << osmFilePath << std::endl;
    if (g_cache.preloadFile(osmFilePath)) {
        std::cout << "Pre-loaded successfully! (" << g_cache.getTotalNodesLoaded() << " nodes)" << std::endl;
    } else {
        std::cout << "Warning: Could not pre-load " << osmFilePath 
                  << ". Regions will be downloaded on demand." << std::endl;
    }

    // Background thread for periodic L1 cache eviction
    std::thread evictionThread([&]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::minutes(10));
            int evicted = g_cache.evictOldRegions();
            if (evicted > 0) {
                std::cout << "Cache eviction: removed " << evicted << " stale regions" << std::endl;
            }
        }
    });
    evictionThread.detach();

    // ==================== API ENDPOINTS ====================

    // Health check
    svr.Get("/api/health", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        response["status"] = "ok";
        response["graph_loaded"] = g_cache.getL1Size() > 0;  // backward compat for mobile app
        response["nodes_count"] = g_cache.getTotalNodesLoaded(); // backward compat
        response["regions_loaded"] = g_cache.getL1Size();
        response["total_nodes"] = g_cache.getTotalNodesLoaded();
        response["cache_type"] = "three-level (L1:memory, L2:disk, L3:overpass)";
        res.set_content(response.dump(), "application/json");
    });

    // Pre-load a region (the app should call this before requesting routes)
    svr.Post("/api/load-region", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        try {
            auto body = json::parse(req.body);
            double lat = body["lat"];
            double lon = body["lon"];
            double radiusKm = body.value("radius_km", 5.0);
            double radiusMeters = radiusKm * 1000.0;

            auto startTime = std::chrono::steady_clock::now();

            int tilesLoaded = g_cache.preloadArea(lat, lon, radiusMeters);

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();

            response["success"] = tilesLoaded > 0;
            response["tiles_loaded"] = tilesLoaded;
            response["total_regions"] = g_cache.getL1Size();
            response["total_nodes"] = g_cache.getTotalNodesLoaded();
            response["load_time_ms"] = elapsed;
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Initialize graph from OSM XML (backward compatible)
    svr.Post("/api/init-graph", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string osmData = body["osm_data"];

            // Parse the data and store in cache as a special tile
            Graph graph;
            std::vector<Poi> pois;
            OsmParser parser;
            char errorBuffer[256] = {0};
            int result = parser.parseString(osmData.c_str(), graph, errorBuffer, 256);

            // Also parse POIs
            tinyxml2::XMLDocument doc;
            if (doc.Parse(osmData.c_str()) == tinyxml2::XML_SUCCESS) {
                tinyxml2::XMLElement* root = doc.RootElement();
                if (root) {
                    tinyxml2::XMLElement* node = root->FirstChildElement("node");
                    while (node) {
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

                                if (key == "name") name = val;

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
                            pois.push_back(poi);
                        }

                        node = node->NextSiblingElement("node");
                    }
                }
            }

            json response;
            if (result == 0) {
                response["success"] = true;
                response["nodes_count"] = graph.nodes.size();
                response["pois_count"] = pois.size();
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

    // Simple A-to-B routing (now uses cache)
    svr.Post("/api/get-route", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto body = json::parse(req.body);
            double startLat = body["start"]["lat"];
            double startLon = body["start"]["lon"];
            double endLat = body["end"]["lat"];
            double endLon = body["end"]["lon"];

            // Use merged graph from all loaded regions for cross-tile routing
            Graph mergedGraph;
            std::vector<Poi> mergedPois;

            // First ensure both endpoints' tiles are loaded
            CachedRegion* startRegion = g_cache.getRegion(startLat, startLon);
            CachedRegion* endRegion = g_cache.getRegion(endLat, endLon);

            if (!startRegion || !endRegion) {
                response["success"] = false;
                response["error"] = "Could not load map data for route endpoints. Error: " + g_cache.getLastError();
                res.status = 503;
                res.set_content(response.dump(), "application/json");
                return;
            }

            // Get merged graph of all loaded tiles
            if (!g_cache.getMergedGraph(mergedGraph, mergedPois)) {
                response["success"] = false;
                response["error"] = "No graph data available";
                res.status = 500;
                res.set_content(response.dump(), "application/json");
                return;
            }

            // Find nearest nodes
            int64_t startNode = NearestNeighbor::findNearestNode(mergedGraph, startLat, startLon);
            int64_t endNode = NearestNeighbor::findNearestNode(mergedGraph, endLat, endLon);

            if (startNode == -1 || endNode == -1) {
                response["success"] = false;
                response["error"] = "Could not find nearest nodes";
                res.status = 400;
                res.set_content(response.dump(), "application/json");
                return;
            }

            // Run Dijkstra
            Dijkstra dijkstra;
            std::vector<int64_t> path = dijkstra.findShortestPath(mergedGraph, startNode, endNode);

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
                Node& n = mergedGraph.nodes[path[i]];
                route.push_back({{"lat", n.lat}, {"lon", n.lon}});

                if (i > 0) {
                    for (const auto& edge : mergedGraph.adjacency_list[path[i-1]]) {
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

    // Multi-POI circular route optimization (now uses cache)
    svr.Post("/api/optimize-route", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto body = json::parse(req.body);
            double startLat = body["start"]["lat"];
            double startLon = body["start"]["lon"];
            double targetDistance = body["target_distance_meters"];

            // Ensure the start region is loaded
            CachedRegion* startRegion = g_cache.getRegion(startLat, startLon);
            if (!startRegion) {
                response["success"] = false;
                response["error"] = "Could not load map data for start location. Error: " + g_cache.getLastError();
                res.status = 503;
                res.set_content(response.dump(), "application/json");
                return;
            }

            // Also load regions for all POIs
            for (const auto& poi : body["pois"]) {
                g_cache.getRegion(poi["lat"].get<double>(), poi["lon"].get<double>());
            }

            // Get merged graph
            Graph mergedGraph;
            std::vector<Poi> mergedPois;
            if (!g_cache.getMergedGraph(mergedGraph, mergedPois)) {
                response["success"] = false;
                response["error"] = "No graph data available";
                res.status = 500;
                res.set_content(response.dump(), "application/json");
                return;
            }

            int64_t startNode = NearestNeighbor::findNearestNode(mergedGraph, startLat, startLon);
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
                int64_t nodeId = NearestNeighbor::findNearestNode(mergedGraph, lat, lon);
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

            PctspOptimizer optimizer(mergedGraph);
            OptimizedRoute result = optimizer.optimize(startNode, pois, targetDistance);

            if (result.nodeIds.empty()) {
                response["success"] = false;
                response["error"] = "Could not generate route";
                res.set_content(response.dump(), "application/json");
                return;
            }

            json route = json::array();
            for (int64_t nodeId : result.nodeIds) {
                if (mergedGraph.nodes.find(nodeId) != mergedGraph.nodes.end()) {
                    Node& n = mergedGraph.nodes[nodeId];
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

    // Get nearest walkable node (now uses cache)
    svr.Post("/api/nearest-node", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto body = json::parse(req.body);
            double lat = body["lat"];
            double lon = body["lon"];

            CachedRegion* region = g_cache.getRegion(lat, lon);
            if (!region) {
                response["success"] = false;
                response["error"] = "Could not load map data. Error: " + g_cache.getLastError();
                res.status = 503;
                res.set_content(response.dump(), "application/json");
                return;
            }

            int64_t nodeId = NearestNeighbor::findNearestNode(region->graph, lat, lon);

            if (nodeId == -1) {
                response["success"] = false;
                response["error"] = "No node found";
                res.set_content(response.dump(), "application/json");
                return;
            }

            Node& n = region->graph.nodes[nodeId];
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

    // BATCH: Get nearest walkable nodes (now uses cache)
    svr.Post("/api/nearest-nodes", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto body = json::parse(req.body);
            json results = json::array();

            for (const auto& point : body["points"]) {
                double lat = point["lat"];
                double lon = point["lon"];

                json nodeResult;
                CachedRegion* region = g_cache.getRegion(lat, lon);
                if (region) {
                    int64_t nodeId = NearestNeighbor::findNearestNode(region->graph, lat, lon);
                    if (nodeId != -1) {
                        Node& n = region->graph.nodes[nodeId];
                        nodeResult["success"] = true;
                        nodeResult["lat"] = n.lat;
                        nodeResult["lon"] = n.lon;
                        nodeResult["node_id"] = nodeId;
                    } else {
                        nodeResult["success"] = false;
                        nodeResult["lat"] = lat;
                        nodeResult["lon"] = lon;
                    }
                } else {
                    nodeResult["success"] = false;
                    nodeResult["lat"] = lat;
                    nodeResult["lon"] = lon;
                    nodeResult["error"] = "Could not load region";
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

    // Get POIs within radius (now uses cache)
    svr.Post("/api/pois", [](const httplib::Request& req, httplib::Response& res) {
        json response;

        try {
            auto body = json::parse(req.body);
            double centerLat = body["lat"];
            double centerLon = body["lon"];
            double radius = body.value("radius", 1000.0);

            // Load the region for the center point
            CachedRegion* region = g_cache.getRegion(centerLat, centerLon);
            if (!region) {
                response["success"] = false;
                response["error"] = "Could not load map data. Error: " + g_cache.getLastError();
                res.status = 503;
                res.set_content(response.dump(), "application/json");
                return;
            }

            auto haversine = [](double lat1, double lon1, double lat2, double lon2) -> double {
                const double R = 6371000.0;
                double dLat = (lat2 - lat1) * M_PI / 180.0;
                double dLon = (lon2 - lon1) * M_PI / 180.0;
                double a = sin(dLat / 2) * sin(dLat / 2) +
                           cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                           sin(dLon / 2) * sin(dLon / 2);
                double c = 2 * atan2(sqrt(a), sqrt(1 - a));
                return R * c;
            };

            json poisArr = json::array();
            for (const auto& poi : region->pois) {
                double dist = haversine(centerLat, centerLon, poi.lat, poi.lon);
                if (dist <= radius) {
                    poisArr.push_back({
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
            response["pois"] = poisArr;
            response["count"] = poisArr.size();
            response["total_pois_in_region"] = region->pois.size();
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            res.status = 400;
            res.set_content(response.dump(), "application/json");
        }
    });

    // Cache management endpoints
    svr.Get("/api/cache/status", [](const httplib::Request& req, httplib::Response& res) {
        json response;
        response["l1_regions"] = g_cache.getL1Size();
        response["total_nodes"] = g_cache.getTotalNodesLoaded();
        res.set_content(response.dump(), "application/json");
    });

    svr.Post("/api/cache/evict", [](const httplib::Request& req, httplib::Response& res) {
        int evicted = g_cache.evictOldRegions();
        json response;
        response["evicted"] = evicted;
        response["remaining"] = g_cache.getL1Size();
        res.set_content(response.dump(), "application/json");
    });

    std::cout << "Starting server on http://0.0.0.0:8080" << std::endl;
    std::cout << "Cache directory: cache/" << std::endl;
    std::cout << "L1 TTL: 7200 seconds (2 hours)" << std::endl;
    svr.listen("0.0.0.0", 8080);

    return 0;
}