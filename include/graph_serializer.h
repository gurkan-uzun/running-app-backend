#ifndef GRAPH_SERIALIZER_H
#define GRAPH_SERIALIZER_H

#include "graph.h"
#include "poi.h"
#include <string>
#include <vector>

/// Serializes and deserializes Graph + POI data to/from a compact binary format.
/// This avoids re-parsing XML on repeat loads, giving ~100x faster load times.
///
/// Binary format (v1):
///   [4 bytes] magic "RGRH"
///   [4 bytes] format version (1)
///   [4 bytes] node count
///   [4 bytes] edge count  (total directed edges)
///   [4 bytes] poi count
///   [per node] int64_t id, double lat, double lon
///   [per edge] int64_t from, int64_t to, double weight
///   [per poi]  int64_t id, double lat, double lon,
///              uint16_t nameLen, char[] name,
///              uint16_t catLen, char[] category,
///              uint16_t rawCatLen, char[] rawCategory
class GraphSerializer {
public:
    static constexpr uint32_t MAGIC = 0x48524752; // "RGRH" in little-endian
    static constexpr uint32_t FORMAT_VERSION = 1;

    /// Serialize a Graph and its POIs to a binary file.
    /// Returns true on success.
    static bool serialize(const Graph& graph, const std::vector<Poi>& pois, const std::string& filepath);

    /// Deserialize a binary file into a Graph and POI vector.
    /// Returns true on success. Graph and pois are cleared before loading.
    static bool deserialize(const std::string& filepath, Graph& graph, std::vector<Poi>& pois);
};

#endif // GRAPH_SERIALIZER_H
