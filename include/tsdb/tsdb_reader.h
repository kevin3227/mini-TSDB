#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"
#include <vector>
#include <utility>
#include <unordered_map>
#include <map>
#include <list>
#include <functional>

namespace tsdb {

// 索引结构：记录块的时间范围与位置
struct BlockInfo {
    uint64_t min_ts;  // 块中最小时间戳
    uint64_t offset;  // 块在文件中的偏移
    uint32_t size;    // 块大小
};

// 时间范围键
struct TimeRange {
    uint64_t start;
    uint64_t end;
    
    bool operator==(const TimeRange& other) const {
        return start == other.start && end == other.end;
    }
};

// TimeRange哈希函数
struct TimeRangeHash {
    std::size_t operator()(const TimeRange& range) const {
        return std::hash<uint64_t>()(range.start) ^ 
               (std::hash<uint64_t>()(range.end) << 1);
    }
};

class TSDBReader {
public:
    // 构造函数新增缓存容量参数
    explicit TSDBReader(const std::string& path, size_t cache_capacity = 50);
    
    // 析构函数
    ~TSDBReader();
    
    // 查询时间范围内的数据点 [start, end]
    std::vector<std::pair<uint64_t, double>> query(uint64_t start, uint64_t end);
    
    // 获取缓存命中统计
    size_t getCacheHits() const { return cache_hits_; }
    size_t getCacheMisses() const { return cache_misses_; }
    
    // 清除缓存
    void clearCache();

private:
    // 用于跟踪索引加载的状态
    struct IndexStatus {
        bool fully_loaded = false;
        uint64_t min_loaded_ts = UINT64_MAX;
        uint64_t max_loaded_ts = 0;
    } index_status_;
    
    // LRU缓存节点结构
    struct LRUCacheNode {
        TimeRange range;
        std::vector<std::pair<uint64_t, double>> data;
    };
    
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
    
    // 缓存查询方法
    bool lookupCache(uint64_t start, uint64_t end, 
                    std::vector<std::pair<uint64_t, double>>& result);
    
    // 更新缓存方法
    void updateCache(uint64_t start, uint64_t end,
                   const std::vector<std::pair<uint64_t, double>>& data);

    MMapFile mmap_file_;  // mmap文件管理
    const uint8_t* data_;  // 映射的数据指针
    const uint8_t* end_;   // 数据结束位置
    
    // 索引数据结构
    std::map<uint64_t, BlockInfo> time_blocks_;
    
    bool index_loaded_ = false;
    std::mutex index_mutex_;  // 保护索引访问的互斥锁
    
    // LRU缓存相关成员
    size_t cache_capacity_;  // 缓存容量
    std::mutex cache_mutex_;  // 缓存访问互斥锁
    
    // LRU缓存实现
    std::list<LRUCacheNode> cache_list_;
    
    // 哈希表：时间范围 -> 链表迭代器
    std::unordered_map<TimeRange, 
                      std::list<LRUCacheNode>::iterator, 
                      TimeRangeHash> cache_map_;
    
    // 缓存统计
    size_t cache_hits_ = 0;
    size_t cache_misses_ = 0;
};

} // namespace tsdb
