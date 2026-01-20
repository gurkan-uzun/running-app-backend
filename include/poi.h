#ifndef POI_H
#define POI_H

#include <string>
#include <cstdint>

/// Point of Interest extracted from OSM data
struct Poi {
    int64_t id;
    double lat;
    double lon;
    std::string name;
    std::string category;     // Normalized category (park, museum, cafe, etc.)
    std::string rawCategory;  // Original OSM tag value
};

#endif // POI_H
