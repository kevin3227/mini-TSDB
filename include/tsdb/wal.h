#pragma once

#include "tsdb/mmap_file.h"
#include <vector>
#include <functional>

namespace tsdb {

// WAL操作类型
enum WALOpType : uint8_t {
    OP_POINT = 1,
    OP_BATCH = 2,
};

// 时序数据点
struct TimePoint {
    uint64_t timestamp;
    double value;
    
    bool operator<(const TimePoint& other) const {
        return timestamp < other.timestamp;
    }
};

class WALWriter {
public:
    WALWriter(const std::string& path, size_t initial_size = 16 * 1024 * 1024);
    ~WALWriter();
    
    // 记录单个数据点
    void logPoint(uint32_t shard_id, uint64_t timestamp, double value);
    
    // 记录批量数据点
    void logBatch(uint32_t shard_id, const std::vector<TimePoint>& points);
    
    // 标记某个shard已处理到某个位置
    void markProcessed(uint32_t shard_id, uint64_t offset);
    
    // 获取某个shard的恢复点
    uint64_t getRecoveryPoint(uint32_t shard_id);

    // 获取当前WAL写入位置
    uint64_t getCurrentPosition();
    
    // 执行恢复操作，返回恢复的点数
    size_t recover(std::function<void(uint32_t, uint64_t, double)> point_handler);
    
    // 关闭WAL
    void close();
    
private:
    MMapFile wal_file_;
    std::mutex wal_mutex_;
    std::string path_;
    std::atomic<uint64_t> total_entries_{0};
    std::vector<uint64_t> shard_positions_; // 每个分片的处理位置
    
    // 将checkpoint信息写入元数据文件
    void writeCheckpoint();
    
    // 从元数据文件读取checkpoint信息
    void readCheckpoint();
};

} // namespace tsdb
