add_executable(write_benchmark write_benchmark.cpp ../src/tsdb.cpp ../src/mmap_file.cpp ../src/delta_delta.cpp)
target_include_directories(write_benchmark PRIVATE ../include)
