cmake_minimum_required(VERSION 3.12)
project(tsdb_tests)

# Enable testing
enable_testing()

# Find dependencies
find_package(GTest REQUIRED)
find_package(Flatbuffers REQUIRED)
find_package(TBB REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/test
    ${CMAKE_SOURCE_DIR}/generated
)

# Define common test libraries
set(TSDB_TEST_LIBS
    GTest::GTest
    GTest::Main
    flatbuffers::flatbuffers
    stdc++fs
    TBB::tbb
    tsdb_core
)

# 1. mmap_file test
add_executable(mmap_file_test mmap_file_test.cpp)
target_link_libraries(mmap_file_test ${TSDB_TEST_LIBS})
add_test(NAME mmap_file_test COMMAND mmap_file_test)

# 2. delta_delta test
add_executable(delta_delta_test delta_delta_test.cpp)
target_link_libraries(delta_delta_test ${TSDB_TEST_LIBS})
add_test(NAME delta_delta_test COMMAND delta_delta_test)

# 3. tsdb_writer integration test
add_executable(tsdb_writer_test tsdb_writer_test.cpp)
target_link_libraries(tsdb_writer_test ${TSDB_TEST_LIBS})
add_test(NAME tsdb_writer_test COMMAND tsdb_writer_test)

# 4. tsdb_reader integration test
add_executable(tsdb_reader_test tsdb_reader_test.cpp)
target_link_libraries(tsdb_reader_test ${TSDB_TEST_LIBS})
add_test(NAME tsdb_reader_test COMMAND tsdb_reader_test)

# 5. WAL test
add_executable(wal_test wal_test.cpp)
target_link_libraries(wal_test ${TSDB_TEST_LIBS})
add_test(NAME wal_test COMMAND wal_test)
