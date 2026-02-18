#include "region_cache.h"
#include "osm_parser.h"
#include "tinyxml2.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cmath>

RegionCache::RegionCache(const std::string& cacheDir, int ttlSeconds)
    : cacheDir_(cacheDir), ttlSeconds_(ttlSeconds) {
    ensureCacheDir();
}

void RegionCache::ensureCacheDir() {
    struct stat st;
    if (stat(cacheDir_.c_str(), &st) != 0) {
        // Directory doesn't exist, create it
#ifdef _WIN32
        mkdir(cacheDir_.c_str());
#else
        mkdir(cacheDir_.c_str(), 0755);
#endif
        std::cout << "RegionCache: Created cache directory: " << cacheDir_ << std::endl;
    }
}

std::string RegionCache::getBinaryPath(const TileKey& key) const {
    return cacheDir_ + "/" + key.toString() + ".bin";
}

CachedRegion* RegionCache::getRegion(double lat, double lon) {
    TileKey key = TileSystem::getTile(lat, lon);
    return getRegionByTile(key);
}

CachedRegion* RegionCache::getRegionByTile(const TileKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    // L1: Check in-memory cache
    auto it = l1Cache_.find(key);
    if (it != l1Cache_.end()) {
        it->second->lastAccessed = std::chrono::steady_clock::now();
        std::cout << "RegionCache: L1 HIT for tile " << key.toString() << std::endl;
        return it->second.get();
    }

    // L2: Check binary cache on disk
    auto region = loadFromDisk(key);
    if (region) {
        std::cout << "RegionCache: L2 HIT for tile " << key.toString() << std::endl;
        l1Cache_[key] = region;
        return region.get();
    }

    // L3: Download from Overpass API
    region = downloadAndCache(key);
    if (region) {
        std::cout << "RegionCache: L3 downloaded tile " << key.toString() << std::endl;
        l1Cache_[key] = region;
        return region.get();
    }

    std::cerr << "RegionCache: Failed to load tile " << key.toString() << std::endl;
    return nullptr;
}

std::shared_ptr<CachedRegion> RegionCache::loadFromDisk(const TileKey& key) {
    std::string path = getBinaryPath(key);

    // Check if file exists
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return nullptr; // File doesn't exist
    }

    auto region = std::make_shared<CachedRegion>();
    region->key = key;

    if (GraphSerializer::deserialize(path, region->graph, region->pois)) {
        region->lastAccessed = std::chrono::steady_clock::now();
        return region;
    }

    return nullptr;
}

std::shared_ptr<CachedRegion> RegionCache::downloadAndCache(const TileKey& key) {
    BoundingBox bbox = TileSystem::getTileBbox(key);

    std::string xmlData = overpassClient_.downloadRegion(bbox);
    if (xmlData.empty()) {
        lastError_ = "Overpass download failed: " + overpassClient_.getLastError();
        std::cerr << "RegionCache: " << lastError_ << std::endl;
        return nullptr;
    }

    auto region = std::make_shared<CachedRegion>();
    region->key = key;

    if (!parseOsmData(xmlData, region->graph, region->pois)) {
        lastError_ = "Failed to parse downloaded OSM data";
        return nullptr;
    }

    // Save to L2 disk cache
    std::string binPath = getBinaryPath(key);
    if (GraphSerializer::serialize(region->graph, region->pois, binPath)) {
        std::cout << "RegionCache: Saved to L2 cache: " << binPath << std::endl;
    } else {
        std::cerr << "RegionCache: Warning - could not save L2 cache for tile " 
                  << key.toString() << std::endl;
    }

    region->lastAccessed = std::chrono::steady_clock::now();
    return region;
}

bool RegionCache::parseOsmData(const std::string& xmlData, Graph& graph, std::vector<Poi>& pois) {
    // Parse graph using OsmParser
    OsmParser parser;
    char errorBuffer[256] = {0};
    int result = parser.parseString(xmlData.c_str(), graph, errorBuffer, 256);

    if (result != 0) {
        lastError_ = std::string("OsmParser error: ") + errorBuffer;
        std::cerr << "RegionCache: " << lastError_ << std::endl;
        return false;
    }

    // Parse POIs from the same XML data
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xmlData.c_str()) == tinyxml2::XML_SUCCESS) {
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

                        if (key == "name") {
                            name = val;
                        }

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

    std::cout << "RegionCache: Parsed " << graph.nodes.size() << " nodes, " 
              << pois.size() << " POIs" << std::endl;
    return true;
}

bool RegionCache::preloadFile(const std::string& osmFilePath) {
    std::ifstream file(osmFilePath);
    if (!file.is_open()) {
        lastError_ = "Could not open file: " + osmFilePath;
        std::cerr << "RegionCache: " << lastError_ << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xmlContent = buffer.str();
    file.close();

    // Parse into a temporary graph to compute the centroid
    Graph tempGraph;
    std::vector<Poi> tempPois;

    if (!parseOsmData(xmlContent, tempGraph, tempPois)) {
        return false;
    }

    if (tempGraph.nodes.empty()) {
        lastError_ = "No nodes found in file: " + osmFilePath;
        return false;
    }

    // Compute centroid of all nodes to determine the tile
    double avgLat = 0, avgLon = 0;
    for (const auto& pair : tempGraph.nodes) {
        avgLat += pair.second.lat;
        avgLon += pair.second.lon;
    }
    avgLat /= tempGraph.nodes.size();
    avgLon /= tempGraph.nodes.size();

    TileKey key = TileSystem::getTile(avgLat, avgLon);

    std::lock_guard<std::mutex> lock(mutex_);

    auto region = std::make_shared<CachedRegion>();
    region->key = key;
    region->graph = std::move(tempGraph);
    region->pois = std::move(tempPois);
    region->lastAccessed = std::chrono::steady_clock::now();

    l1Cache_[key] = region;

    // Also save to L2 for fast reload next time
    std::string binPath = getBinaryPath(key);
    GraphSerializer::serialize(region->graph, region->pois, binPath);

    std::cout << "RegionCache: Pre-loaded file " << osmFilePath 
              << " into tile " << key.toString()
              << " (centroid: " << avgLat << ", " << avgLon << ")" << std::endl;

    return true;
}

int RegionCache::preloadArea(double lat, double lon, double radiusMeters) {
    std::vector<TileKey> tiles = TileSystem::getRequiredTiles(lat, lon, radiusMeters);
    int loaded = 0;

    for (const auto& tile : tiles) {
        CachedRegion* region = getRegionByTile(tile);
        if (region) {
            loaded++;
        } else {
            std::cerr << "RegionCache: Failed to load tile " << tile.toString() 
                      << " during area preload" << std::endl;
        }
    }

    std::cout << "RegionCache: Pre-loaded " << loaded << "/" << tiles.size() 
              << " tiles for area around (" << lat << ", " << lon << ")" << std::endl;
    return loaded;
}

int RegionCache::evictOldRegions() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    int evicted = 0;

    auto it = l1Cache_.begin();
    while (it != l1Cache_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second->lastAccessed).count();

        if (age > ttlSeconds_) {
            std::cout << "RegionCache: Evicting tile " << it->first.toString() 
                      << " (age: " << age << "s)" << std::endl;
            it = l1Cache_.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }

    return evicted;
}

size_t RegionCache::getL1Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return l1Cache_.size();
}

size_t RegionCache::getTotalNodesLoaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& pair : l1Cache_) {
        total += pair.second->graph.nodes.size();
    }
    return total;
}

bool RegionCache::getMergedGraph(Graph& outGraph, std::vector<Poi>& outPois) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (l1Cache_.empty()) return false;

    outGraph.nodes.clear();
    outGraph.adjacency_list.clear();
    outPois.clear();

    for (const auto& pair : l1Cache_) {
        const auto& region = pair.second;

        // Merge nodes
        for (const auto& nodePair : region->graph.nodes) {
            outGraph.nodes[nodePair.first] = nodePair.second;
        }

        // Merge adjacency list
        for (const auto& adjPair : region->graph.adjacency_list) {
            auto& edges = outGraph.adjacency_list[adjPair.first];
            edges.insert(edges.end(), adjPair.second.begin(), adjPair.second.end());
        }

        // Merge POIs
        outPois.insert(outPois.end(), region->pois.begin(), region->pois.end());
    }

    return true;
}
