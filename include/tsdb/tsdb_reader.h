#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"
#include <vector>
#include <utility>
#include <unordered_map>
#include <map>

namespace tsdb {

// 索引结构：记录块的时间范围与位置
struct BlockInfo {
    uint64_t min_ts;  // 块中最小时间戳
    uint64_t offset;  // 块在文件中的偏移
    uint32_t size;    // 块大小
};

class TSDBReader {
public:
    explicit TSDBReader(const std::string& path);
    
    // 查询时间范围内的数据点 [start, end]
    std::vector<std::pair<uint64_t, double>> query(uint64_t start, uint64_t end);

private:
    // 用于跟踪索引加载的状态
    struct IndexStatus {
        bool fully_loaded = false;
        uint64_t min_loaded_ts = UINT64_MAX;
        uint64_t max_loaded_ts = 0;
    } index_status_;
    
    // 懒加载索引方法
    void loadIndexLazy(uint64_t start_ts, uint64_t end_ts);

    // 读取一个带大小前缀的FlatBuffer数据块
    const uint8_t* readNextBlock(const uint8_t* current_pos, uint32_t* out_size);
    
    // 处理单个数据块
    void processBlock(
        const uint8_t* block_data, 
        uint32_t block_size, 
        uint64_t start, 
        uint64_t end, 
        std::vector<std::pair<uint64_t, double>>& results);

    // 加载索引信息
    void loadIndex();

    MMapFile mmap_file_;  // mmap文件管理
    const uint8_t* data_;  // 映射的数据指针
    const uint8_t* end_;   // 数据结束位置
    
    // 改进的索引结构：
    // 1. 按时间戳排序的数据块索引 (min_ts -> 块信息)
    std::map<uint64_t, BlockInfo> time_blocks_;
    // 2. 块偏移量到块信息的映射
    std::unordered_map<uint64_t, BlockInfo> offset_to_block_;
    
    bool index_loaded_ = false;
    std::mutex index_mutex_;  // 保护索引访问的互斥锁
};

} // namespace tsdb
