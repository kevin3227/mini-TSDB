#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"
#include <vector>
#include <utility>

namespace tsdb {

class TSDBReader {
public:
    explicit TSDBReader(const std::string& path);
    
    // 查询时间范围内的数据点 [start, end]
    std::vector<std::pair<uint64_t, double>> query(uint64_t start, uint64_t end);

private:
    MMapFile mmap_file_;  // mmap文件管理
    const uint8_t* data_;  // 映射的数据指针
    size_t size_;         // 数据总大小
};

} // namespace tsdb
