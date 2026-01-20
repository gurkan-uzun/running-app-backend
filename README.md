# Running App Backend Server

C++ REST API server for the Running App route optimization.

## Prerequisites

- CMake 3.14+
- C++17 compiler (clang++ or g++)
- Boost libraries (for Asio)

### macOS
```bash
brew install cmake boost
```

### Ubuntu/Debian
```bash
sudo apt install cmake libboost-all-dev
```

## Building

```bash
mkdir build && cd build
cmake ..
make -j4
```

## Running

```bash
# Default: looks for data/istanbul.osm
./running_app_backend

# Or specify OSM file path
./running_app_backend /path/to/your/map.osm
```

Server starts on `http://localhost:8080`

## API Endpoints

### GET /api/health
Health check and graph status.

### POST /api/init-graph
Load OSM data (XML format).
```json
{"osm_data": "<xml>...</xml>"}
```

### POST /api/get-route
Simple A-to-B routing.
```json
{
  "start": {"lat": 40.99, "lon": 29.02},
  "end": {"lat": 41.00, "lon": 29.03}
}
```

### POST /api/optimize-route
Multi-POI circular route optimization.
```json
{
  "start": {"lat": 40.99, "lon": 29.02},
  "pois": [
    {"lat": 40.991, "lon": 29.021},
    {"lat": 40.992, "lon": 29.022}
  ],
  "target_distance_meters": 10000
}
```

### POST /api/nearest-node
Find nearest walkable graph node.
```json
{"lat": 40.99, "lon": 29.02}
```

## Getting Istanbul OSM Data

Download from Geofabrik:
```bash
wget https://download.geofabrik.de/europe/turkey-latest.osm.pbf
# Convert to XML or use a smaller extract for Istanbul
```

Or use Overpass API for a specific area.
