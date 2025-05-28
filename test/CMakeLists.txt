enable_testing()
find_package(GTest REQUIRED)

add_executable(mmap_file_test mmap_file_test.cpp ../src/mmap_file.cpp)
add_executable(delta_delta_test delta_delta_test.cpp ../src/delta_delta.cpp)
target_link_libraries(mmap_file_test GTest::GTest GTest::Main)

add_test(NAME MMapFileTest COMMAND mmap_file_test)