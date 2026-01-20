cmake_minimum_required(VERSION 3.14)
project(running_app_backend VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/main.cpp
    src/dijkstra.cpp
    src/osm_parser.cpp
    src/pctsp_optimizer.cpp
    src/tinyxml2.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} 
    Threads::Threads
)

# For macOS, link to system libraries
if(APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework CoreFoundation")
endif()

message(STATUS "Building ${PROJECT_NAME} v${PROJECT_VERSION}")