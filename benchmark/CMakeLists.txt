cmake_minimum_required(VERSION 3.12)
project(tsdb_benchmark)

# Enable C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Google Benchmark library
find_package(benchmark REQUIRED)
find_package(TBB REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add benchmark executable
add_executable(benchmark ../benchmark/tsdb_benchmark.cpp)

# Set target properties
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Link dependencies
target_link_libraries(benchmark PRIVATE
    tsdb_core
    benchmark::benchmark
    benchmark::benchmark_main
    flatbuffers
    TBB::tbb
)

# Optional compiler optimizations (non-debug mode)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(benchmark PRIVATE
        -O3
        -march=native
        -mtune=native
    )
endif()
