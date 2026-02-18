#include "overpass_client.h"

// httplib must be included in exactly one translation unit with implementation.
// It's already included in main.cpp, so we just include the header here.
// Since httplib.h has include guards and is header-only, this is safe.
#include "httplib.h"

#include <iostream>
#include <sstream>
#include <chrono>

OverpassClient::OverpassClient(const std::string& host, int port)
    : host_(host), port_(port) {
}

std::string OverpassClient::buildQuery(const BoundingBox& bbox) const {
    std::ostringstream oss;
    oss.precision(6);
    oss << std::fixed;

    // Overpass QL query:
    // - Fetch all highway ways (for routing graph)
    // - Fetch nodes referenced by those ways (via >; recurse down)
    // - Fetch POI nodes (amenity, tourism, leisure, historic, natural)
    oss << "[out:xml][timeout:120];"
        << "("
        // Highway ways for routing
        << "way[\"highway\"](" 
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        // POI nodes
        << "node[\"amenity\"]("
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        << "node[\"tourism\"]("
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        << "node[\"leisure\"]("
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        << "node[\"historic\"]("
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        << "node[\"natural\"]("
        << bbox.south << "," << bbox.west << "," 
        << bbox.north << "," << bbox.east << ");"
        << ");"
        // Recurse down: fetch all nodes referenced by the ways
        << "(._;>;);"
        << "out body;";

    return oss.str();
}

std::string OverpassClient::downloadRegion(const BoundingBox& bbox) {
    lastError_.clear();

    std::string query = buildQuery(bbox);
    std::cout << "OverpassClient: Downloading region ["
              << bbox.south << "," << bbox.west << " -> "
              << bbox.north << "," << bbox.east << "]" << std::endl;

    auto startTime = std::chrono::steady_clock::now();

    // Use httplib to make the POST request
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(30, 0);  // 30 seconds connection timeout
    cli.set_read_timeout(120, 0);       // 120 seconds read timeout (Overpass can be slow)

    // Overpass API expects form-encoded data
    std::string body = "data=" + query;

    auto res = cli.Post("/api/interpreter", body, "application/x-www-form-urlencoded");

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    if (!res) {
        lastError_ = "HTTP request failed (network error or timeout)";
        std::cerr << "OverpassClient: " << lastError_ << " after " << elapsed << "ms" << std::endl;
        return "";
    }

    if (res->status != 200) {
        lastError_ = "HTTP " + std::to_string(res->status) + ": " + res->body.substr(0, 200);
        std::cerr << "OverpassClient: " << lastError_ << std::endl;
        return "";
    }

    std::cout << "OverpassClient: Downloaded " << res->body.size() 
              << " bytes in " << elapsed << "ms" << std::endl;

    return res->body;
}

std::string OverpassClient::downloadTiles(const std::vector<TileKey>& tiles) {
    if (tiles.empty()) {
        lastError_ = "No tiles specified";
        return "";
    }

    // Merge all tiles into one bounding box and download once
    BoundingBox merged = TileSystem::getMergedBbox(tiles);
    return downloadRegion(merged);
}
