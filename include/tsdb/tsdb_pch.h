#pragma once

// C++ 标准库
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <list>
#include <utility>

// 系统库
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <cassert>

// 外部库
#include <flatbuffers/flatbuffers.h>
#include <boost/lockfree/queue.hpp>
#include <boost/align/aligned_allocator.hpp>

// 系统特定头文件
#ifdef _WIN32
  // Windows特定头文件
#else
  // POSIX特定头文件
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <filesystem>
#endif

// 项目内部头文件
#include "tsdb/tsdb_generated.h"