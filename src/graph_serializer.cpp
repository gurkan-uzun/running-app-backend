#include "graph_serializer.h"
#include <fstream>
#include <iostream>
#include <cstring>

bool GraphSerializer::serialize(const Graph& graph, const std::vector<Poi>& pois, const std::string& filepath) {
    std::ofstream out(filepath, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "GraphSerializer: Could not open file for writing: " << filepath << std::endl;
        return false;
    }

    // Header
    uint32_t magic = MAGIC;
    uint32_t version = FORMAT_VERSION;
    uint32_t nodeCount = static_cast<uint32_t>(graph.nodes.size());

    // Count total directed edges
    uint32_t edgeCount = 0;
    for (const auto& pair : graph.adjacency_list) {
        edgeCount += static_cast<uint32_t>(pair.second.size());
    }

    uint32_t poiCount = static_cast<uint32_t>(pois.size());

    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
    out.write(reinterpret_cast<const char*>(&edgeCount), sizeof(edgeCount));
    out.write(reinterpret_cast<const char*>(&poiCount), sizeof(poiCount));

    // Nodes
    for (const auto& pair : graph.nodes) {
        const Node& n = pair.second;
        out.write(reinterpret_cast<const char*>(&n.id), sizeof(n.id));
        out.write(reinterpret_cast<const char*>(&n.lat), sizeof(n.lat));
        out.write(reinterpret_cast<const char*>(&n.lon), sizeof(n.lon));
    }

    // Edges
    for (const auto& pair : graph.adjacency_list) {
        int64_t fromId = pair.first;
        for (const Edge& e : pair.second) {
            out.write(reinterpret_cast<const char*>(&fromId), sizeof(fromId));
            out.write(reinterpret_cast<const char*>(&e.targetNodeId), sizeof(e.targetNodeId));
            out.write(reinterpret_cast<const char*>(&e.weight), sizeof(e.weight));
        }
    }

    // POIs
    for (const Poi& p : pois) {
        out.write(reinterpret_cast<const char*>(&p.id), sizeof(p.id));
        out.write(reinterpret_cast<const char*>(&p.lat), sizeof(p.lat));
        out.write(reinterpret_cast<const char*>(&p.lon), sizeof(p.lon));

        auto writeString = [&out](const std::string& s) {
            uint16_t len = static_cast<uint16_t>(s.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            if (len > 0) {
                out.write(s.c_str(), len);
            }
        };

        writeString(p.name);
        writeString(p.category);
        writeString(p.rawCategory);
    }

    out.close();
    std::cout << "GraphSerializer: Saved " << nodeCount << " nodes, " 
              << edgeCount << " edges, " << poiCount << " POIs to " << filepath << std::endl;
    return true;
}

bool GraphSerializer::deserialize(const std::string& filepath, Graph& graph, std::vector<Poi>& pois) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "GraphSerializer: Could not open file for reading: " << filepath << std::endl;
        return false;
    }

    // Header
    uint32_t magic, version, nodeCount, edgeCount, poiCount;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    in.read(reinterpret_cast<char*>(&edgeCount), sizeof(edgeCount));
    in.read(reinterpret_cast<char*>(&poiCount), sizeof(poiCount));

    if (magic != MAGIC) {
        std::cerr << "GraphSerializer: Invalid magic number in " << filepath << std::endl;
        return false;
    }

    if (version != FORMAT_VERSION) {
        std::cerr << "GraphSerializer: Unsupported format version " << version 
                  << " (expected " << FORMAT_VERSION << ")" << std::endl;
        return false;
    }

    // Clear existing data
    graph.nodes.clear();
    graph.adjacency_list.clear();
    pois.clear();

    // Reserve capacity for better performance
    graph.nodes.reserve(nodeCount);

    // Nodes
    for (uint32_t i = 0; i < nodeCount; ++i) {
        int64_t id;
        double lat, lon;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));
        in.read(reinterpret_cast<char*>(&lat), sizeof(lat));
        in.read(reinterpret_cast<char*>(&lon), sizeof(lon));
        graph.addNode(id, lat, lon);
    }

    // Edges
    for (uint32_t i = 0; i < edgeCount; ++i) {
        int64_t from, to;
        double weight;
        in.read(reinterpret_cast<char*>(&from), sizeof(from));
        in.read(reinterpret_cast<char*>(&to), sizeof(to));
        in.read(reinterpret_cast<char*>(&weight), sizeof(weight));
        graph.addEdge(from, to, weight);
    }

    // POIs
    pois.reserve(poiCount);
    for (uint32_t i = 0; i < poiCount; ++i) {
        Poi p;
        in.read(reinterpret_cast<char*>(&p.id), sizeof(p.id));
        in.read(reinterpret_cast<char*>(&p.lat), sizeof(p.lat));
        in.read(reinterpret_cast<char*>(&p.lon), sizeof(p.lon));

        auto readString = [&in]() -> std::string {
            uint16_t len;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len == 0) return "";
            std::string s(len, '\0');
            in.read(&s[0], len);
            return s;
        };

        p.name = readString();
        p.category = readString();
        p.rawCategory = readString();
        pois.push_back(std::move(p));
    }

    in.close();
    std::cout << "GraphSerializer: Loaded " << nodeCount << " nodes, " 
              << edgeCount << " edges, " << poiCount << " POIs from " << filepath << std::endl;
    return true;
}
