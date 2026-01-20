#ifndef OSM_PARSER_H
#define OSM_PARSER_H

#include <string>

class OsmParser {
public:
    OsmParser();
    ~OsmParser();

    int parse(const std::string& filepath, class Graph& graph);
    int parseString(const char* xmlData, class Graph& graph, char* errorOut, int errorBufSize);

private:
    double toRadians(double distinct);
    double calculateDistance(double lat1, double lon1, double lat2, double lon2);
};

#endif // OSM_PARSER_H
