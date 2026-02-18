#ifndef OVERPASS_CLIENT_H
#define OVERPASS_CLIENT_H

#include "tile_system.h"
#include <string>

/// Client for downloading OSM data from the Overpass API.
/// Uses httplib.h in HTTP client mode.
class OverpassClient {
public:
    /// Construct with optional custom Overpass API endpoint.
    /// Default: "overpass-api.de"
    explicit OverpassClient(const std::string& host = "overpass-api.de", int port = 80);

    /// Download OSM XML data for the given bounding box.
    /// Returns the raw XML string on success, empty string on failure.
    /// The query fetches highways (for routing) and POI nodes.
    std::string downloadRegion(const BoundingBox& bbox);

    /// Download OSM XML data for multiple tiles merged into one bounding box.
    std::string downloadTiles(const std::vector<TileKey>& tiles);

    /// Get the last error message (if downloadRegion returned empty).
    std::string getLastError() const { return lastError_; }

private:
    std::string host_;
    int port_;
    std::string lastError_;

    /// Build the Overpass QL query for the given bounding box.
    std::string buildQuery(const BoundingBox& bbox) const;
};

#endif // OVERPASS_CLIENT_H
