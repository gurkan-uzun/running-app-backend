#ifndef TILE_SYSTEM_H
#define TILE_SYSTEM_H

#include <vector>
#include <cmath>
#include <string>
#include <functional>

// Tile size in degrees (~5.5km at mid-latitudes)
constexpr double TILE_SIZE_DEG = 0.05;

struct BoundingBox {
    double south;
    double west;
    double north;
    double east;
};

struct TileKey {
    int latCell;
    int lonCell;

    bool operator==(const TileKey& other) const {
        return latCell == other.latCell && lonCell == other.lonCell;
    }

    bool operator!=(const TileKey& other) const {
        return !(*this == other);
    }

    // String representation for filenames: e.g. "819_580"
    std::string toString() const {
        return std::to_string(latCell) + "_" + std::to_string(lonCell);
    }
};

// Hash function for TileKey so it can be used in unordered_map
struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const {
        auto h1 = std::hash<int>{}(k.latCell);
        auto h2 = std::hash<int>{}(k.lonCell);
        return h1 ^ (h2 << 16);
    }
};

class TileSystem {
public:
    // Convert (lat, lon) to the tile key it belongs to
    static TileKey getTile(double lat, double lon) {
        TileKey key;
        key.latCell = static_cast<int>(std::floor(lat / TILE_SIZE_DEG));
        key.lonCell = static_cast<int>(std::floor(lon / TILE_SIZE_DEG));
        return key;
    }

    // Get the bounding box for a tile
    static BoundingBox getTileBbox(const TileKey& key) {
        BoundingBox bbox;
        bbox.south = key.latCell * TILE_SIZE_DEG;
        bbox.west = key.lonCell * TILE_SIZE_DEG;
        bbox.north = (key.latCell + 1) * TILE_SIZE_DEG;
        bbox.east = (key.lonCell + 1) * TILE_SIZE_DEG;
        return bbox;
    }

    // Get the center point of a tile
    static void getTileCenter(const TileKey& key, double& lat, double& lon) {
        lat = (key.latCell + 0.5) * TILE_SIZE_DEG;
        lon = (key.lonCell + 0.5) * TILE_SIZE_DEG;
    }

    // Get all tiles needed to cover a circle of given radius around (lat, lon).
    // Always includes the center tile plus any neighboring tiles when the point
    // is within `margin` of a tile boundary.
    static std::vector<TileKey> getRequiredTiles(double lat, double lon, double radiusMeters) {
        // Convert radius to approximate degrees
        // 1 degree of latitude ≈ 111,320 meters
        double radiusDeg = radiusMeters / 111320.0;

        // Compute the range of tile cells covered
        int minLatCell = static_cast<int>(std::floor((lat - radiusDeg) / TILE_SIZE_DEG));
        int maxLatCell = static_cast<int>(std::floor((lat + radiusDeg) / TILE_SIZE_DEG));
        int minLonCell = static_cast<int>(std::floor((lon - radiusDeg) / TILE_SIZE_DEG));
        int maxLonCell = static_cast<int>(std::floor((lon + radiusDeg) / TILE_SIZE_DEG));

        std::vector<TileKey> tiles;
        for (int latC = minLatCell; latC <= maxLatCell; ++latC) {
            for (int lonC = minLonCell; lonC <= maxLonCell; ++lonC) {
                tiles.push_back({latC, lonC});
            }
        }
        return tiles;
    }

    // Merge bounding boxes of multiple tiles into one
    static BoundingBox getMergedBbox(const std::vector<TileKey>& tiles) {
        if (tiles.empty()) return {0, 0, 0, 0};

        BoundingBox merged = getTileBbox(tiles[0]);
        for (size_t i = 1; i < tiles.size(); ++i) {
            BoundingBox b = getTileBbox(tiles[i]);
            if (b.south < merged.south) merged.south = b.south;
            if (b.west < merged.west) merged.west = b.west;
            if (b.north > merged.north) merged.north = b.north;
            if (b.east > merged.east) merged.east = b.east;
        }
        return merged;
    }
};

#endif // TILE_SYSTEM_H
