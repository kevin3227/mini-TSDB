cmake_minimum_required(VERSION 3.12)
project(tsdb_benchmark)

# 启用C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找Google Benchmark库
find_package(benchmark REQUIRED)

# 包含目录设置
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include  # 项目头文件
    ${CMAKE_CURRENT_SOURCE_DIR}              # 当前目录的头文件
)

# 添加benchmark可执行文件
add_executable(benchmark 
    ../benchmark/tsdb_benchmark.cpp
    ../src/tsdb.cpp
    ../src/mmap_file.cpp
    ../src/delta_delta.cpp
    ../src/tsdb_writer.cpp
    ../src/tsdb_reader.cpp
    ../src/tsdb_index.cpp
)

# 设置目标属性
target_include_directories(benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# 链接依赖库
target_link_libraries(benchmark PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    ${CMAKE_THREAD_LIBS_INIT}
    flatbuffers
)

# 可选的编译器优化（非调试模式）
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(benchmark PRIVATE
        -O3
        -march=native
        -mtune=native
    )
endif()
