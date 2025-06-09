cmake_minimum_required(VERSION 3.14)
project(tsdb_engine)

set(CMAKE_CXX_STANDARD 17)

find_package(FlatBuffers REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_subdirectory(src)
add_subdirectory(test)
# add_subdirectory(benchmark)

# message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
# message(STATUS "CMAKE_CURRENT_BINARY_DIR = ${CMAKE_CURRENT_BINARY_DIR}")
