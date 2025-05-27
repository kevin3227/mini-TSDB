enable_testing()
find_package(GTest REQUIRED)

add_executable(mmap_file_test mmap_file_test.cpp ../src/mmap_file.cpp)
target_link_libraries(mmap_file_test GTest::GTest GTest::Main)

add_test(NAME MMapFileTest COMMAND mmap_file_test)