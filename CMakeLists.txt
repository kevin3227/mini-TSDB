cmake_minimum_required(VERSION 3.14)
project(tsdb_engine)

set(CMAKE_CXX_STANDARD 17)

find_package(FlatBuffers REQUIRED)
find_package(Threads REQUIRED)

include(CMakePrecompiledHeader.cmake)

# Set up include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Define common libraries and dependencies
set(COMMON_LIBS Threads::Threads)

# Define source files once to avoid duplication
set(TSDB_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/mmap_file.cpp
    ${PROJECT_SOURCE_DIR}/src/delta_delta.cpp
    ${PROJECT_SOURCE_DIR}/src/wal.cpp
    ${PROJECT_SOURCE_DIR}/src/tsdb_writer.cpp
    ${PROJECT_SOURCE_DIR}/src/tsdb_reader.cpp
)

# Create a static library with precompiled headers
add_library(tsdb_core STATIC ${TSDB_CORE_SOURCES})
target_precompile_headers(tsdb_core PUBLIC ${PROJECT_SOURCE_DIR}/include/tsdb/tsdb_pch.h)
target_link_libraries(tsdb_core PUBLIC ${COMMON_LIBS})

add_subdirectory(src)
add_subdirectory(test)
add_subdirectory(benchmark)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(benchmark PRIVATE -fsanitize=address)
    target_link_options(benchmark PRIVATE -fsanitize=address)
endif()
