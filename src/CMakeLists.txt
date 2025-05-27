add_executable(main main.cpp tsdb.cpp mmap_file.cpp delta_delta.cpp)
target_include_directories(main PRIVATE ../include)
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)
