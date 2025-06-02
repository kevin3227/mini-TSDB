cmake_minimum_required(VERSION 3.12)
project(tsdb_tests)

# 启用测试
enable_testing()

# 查找依赖
find_package(GTest REQUIRED)
find_package(Flatbuffers REQUIRED)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/generated  # FlatBuffers生成的头文件
)

# 定义公共测试依赖
set(TSDB_TEST_LIBS
    GTest::GTest
    GTest::Main
    flatbuffers::flatbuffers
)

# 1. mmap_file 测试
add_executable(mmap_file_test 
    mmap_file_test.cpp 
    ${CMAKE_SOURCE_DIR}/src/mmap_file.cpp
)
target_link_libraries(mmap_file_test ${TSDB_TEST_LIBS})
target_include_directories(mmap_file_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME mmap_file_test COMMAND mmap_file_test)

# 2. delta_delta 测试
add_executable(delta_delta_test 
    delta_delta_test.cpp 
    ${CMAKE_SOURCE_DIR}/src/delta_delta.cpp
)
target_link_libraries(delta_delta_test ${TSDB_TEST_LIBS})
target_include_directories(delta_delta_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME delta_delta_test COMMAND delta_delta_test)

# 3. tsdb_writer 集成测试
add_executable(tsdb_writer_test 
    tsdb_writer_test.cpp 
    ${CMAKE_SOURCE_DIR}/src/tsdb_writer.cpp
    ${CMAKE_SOURCE_DIR}/src/mmap_file.cpp
    ${CMAKE_SOURCE_DIR}/src/delta_delta.cpp
)
target_link_libraries(tsdb_writer_test ${TSDB_TEST_LIBS})
target_include_directories(tsdb_writer_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME tsdb_writer_test COMMAND tsdb_writer_test)

# 4. tsdb_reader 集成测试
add_executable(tsdb_reader_test 
    tsdb_reader_test.cpp 
    ${CMAKE_SOURCE_DIR}/src/tsdb_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/mmap_file.cpp
    ${CMAKE_SOURCE_DIR}/src/delta_delta.cpp
    # ${CMAKE_SOURCE_DIR}/generated/tsdb_generated.cpp  # FlatBuffers生成的代码
)
target_link_libraries(tsdb_reader_test ${TSDB_TEST_LIBS})
target_include_directories(tsdb_reader_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME tsdb_reader_test COMMAND tsdb_reader_test)

# # 5. 端到端测试
# add_executable(end_to_end_test 
#     end_to_end_test.cpp 
#     ${CMAKE_SOURCE_DIR}/src/tsdb_writer.cpp
#     ${CMAKE_SOURCE_DIR}/src/tsdb_reader.cpp
#     ${CMAKE_SOURCE_DIR}/src/mmap_file.cpp
#     ${CMAKE_SOURCE_DIR}/src/delta_delta.cpp
#     ${CMAKE_SOURCE_DIR}/generated/tsdb_generated.cpp
# )
# target_link_libraries(end_to_end_test ${TSDB_TEST_LIBS})
# target_include_directories(end_to_end_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
# add_test(NAME end_to_end_test COMMAND end_to_end_test)

# # 6. 可选的性能测试目标（不参与常规测试）
# add_executable(benchmark_test
#     benchmark_test.cpp
#     ${CMAKE_SOURCE_DIR}/src/tsdb_writer.cpp
#     ${CMAKE_SOURCE_DIR}/src/tsdb_reader.cpp
#     ${CMAKE_SOURCE_DIR}/src/mmap_file.cpp
#     ${CMAKE_SOURCE_DIR}/src/delta_delta.cpp
#     ${CMAKE_SOURCE_DIR}/generated/tsdb_generated.cpp
# )
# target_link_libraries(benchmark_test ${TSDB_TEST_LIBS})
# target_include_directories(benchmark_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
