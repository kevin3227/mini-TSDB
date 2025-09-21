# Use the precompiled headers from tsdb_core
add_executable(main main.cpp tsdb.cpp)
target_include_directories(main PRIVATE ../include)
target_link_libraries(main PRIVATE tsdb_core ${COMMON_LIBS})
