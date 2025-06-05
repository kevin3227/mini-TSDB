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
    // 读取一个带大小前缀的FlatBuffer数据块
    const uint8_t* readNextBlock(const uint8_t* current_pos, uint32_t* out_size);

    MMapFile mmap_file_;  // mmap文件管理
    const uint8_t* data_;  // 映射的数据指针
    const uint8_t* end_;   // 数据结束位置
};

} // namespace tsdb
