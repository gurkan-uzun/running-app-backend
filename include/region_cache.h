#ifndef REGION_CACHE_H
#define REGION_CACHE_H

#include "graph.h"
#include "poi.h"
#include "tile_system.h"
#include "overpass_client.h"
#include "graph_serializer.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <memory>

/// A cached region containing the parsed graph and POIs for one tile.
struct CachedRegion {
    TileKey key;
    Graph graph;
    std::vector<Poi> pois;
    std::chrono::steady_clock::time_point lastAccessed;

    CachedRegion() : lastAccessed(std::chrono::steady_clock::now()) {}
};

/// Three-level cache manager for OSM map data.
///
/// L1: In-memory Graph objects (fastest, limited by RAM)
/// L2: Binary graph files on disk (fast, limited by disk)
/// L3: Overpass API download (slow, unlimited coverage)
///
/// Thread-safe: all public methods lock internally.
class RegionCache {
public:
    /// Construct with cache directory path and L1 TTL.
    /// cacheDir: directory for binary graph files (created if needed)
    /// ttlSeconds: how long L1 entries stay before eviction (default 2 hours)
    explicit RegionCache(const std::string& cacheDir = "cache", int ttlSeconds = 7200);

    /// Get the graph and POIs for the tile containing (lat, lon).
    /// Flows through L1 → L2 → L3 as needed.
    /// Returns pointer to cached region, or nullptr on failure.
    /// The returned pointer is valid until evictOldRegions() is called.
    CachedRegion* getRegion(double lat, double lon);

    /// Get a region by explicit tile key.
    CachedRegion* getRegionByTile(const TileKey& key);

    /// Pre-load a local .osm file into the cache.
    /// Computes the tile key from the data's centroid.
    /// Returns true on success.
    bool preloadFile(const std::string& osmFilePath);

    /// Pre-load tiles for a given area (lat, lon, radius in meters).
    /// Downloads from Overpass if not cached. Returns number of tiles loaded.
    int preloadArea(double lat, double lon, double radiusMeters);

    /// Evict L1 entries older than the TTL.
    /// Returns number of entries evicted.
    int evictOldRegions();

    /// Get cache statistics.
    size_t getL1Size() const;
    size_t getTotalNodesLoaded() const;

    /// Get a combined graph that merges all currently loaded L1 regions.
    /// This is used when the user's route might cross tile boundaries.
    /// Returns false if no regions are loaded.
    bool getMergedGraph(Graph& outGraph, std::vector<Poi>& outPois);

    /// Get the last error message.
    std::string getLastError() const { return lastError_; }

private:
    std::string cacheDir_;
    int ttlSeconds_;
    mutable std::mutex mutex_;
    std::unordered_map<TileKey, std::shared_ptr<CachedRegion>, TileKeyHash> l1Cache_;
    OverpassClient overpassClient_;
    std::string lastError_;

    /// Get the binary cache file path for a tile.
    std::string getBinaryPath(const TileKey& key) const;

    /// Ensure the cache directory exists.
    void ensureCacheDir();

    /// Try to load from L2 (disk binary cache).
    /// Returns shared_ptr to loaded region, or nullptr on miss.
    std::shared_ptr<CachedRegion> loadFromDisk(const TileKey& key);

    /// Download from L3 (Overpass API), parse, save to L2, and return.
    /// Returns shared_ptr to loaded region, or nullptr on failure.
    std::shared_ptr<CachedRegion> downloadAndCache(const TileKey& key);

    /// Parse raw OSM XML using OsmParser and extract POIs.
    /// Returns true on success.
    bool parseOsmData(const std::string& xmlData, Graph& graph, std::vector<Poi>& pois);
};

#endif // REGION_CACHE_H
