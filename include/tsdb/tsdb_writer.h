#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"

#include <vector>
#include <string>
#include <flatbuffers/flatbuffers.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <boost/lockfree/queue.hpp>

namespace tsdb {

// 时序数据点
struct TimePoint {
    uint64_t timestamp;
    double value;
    
    bool operator<(const TimePoint& other) const {
        return timestamp < other.timestamp;
    }
};

// 自定义内存分配器
class MemoryPool {
public:
    static constexpr size_t SMALL_BLOCK_SIZE = 256;      // 小块大小
    static constexpr size_t MEDIUM_BLOCK_SIZE = 4096;    // 中块大小
    static constexpr size_t LARGE_BLOCK_SIZE = 32768;    // 大块大小
    static constexpr size_t SMALL_BLOCKS_COUNT = 1024;   // 小块数量
    static constexpr size_t MEDIUM_BLOCKS_COUNT = 256;   // 中块数量
    static constexpr size_t LARGE_BLOCKS_COUNT = 64;     // 大块数量

    // 构造函数: 预分配内存池
    MemoryPool();
    
    // 析构函数: 释放所有内存
    ~MemoryPool();
    
    // 从池中分配内存
    void* allocate(size_t size);
    
    // 返回内存到池
    void deallocate(void* ptr, size_t size);
    
    // 统计信息
    size_t getAllocationCount() const { return allocation_count_; }
    size_t getHitCount() const { return hit_count_; }
    size_t getMissCount() const { return miss_count_; }
    
private:
    // 内存块结构
    struct MemoryBlock {
        void* data;
        bool used;
    };

    // 不同大小的内存块池
    std::vector<MemoryBlock> small_blocks_;
    std::vector<MemoryBlock> medium_blocks_;
    std::vector<MemoryBlock> large_blocks_;
    
    // 统计计数器
    std::atomic<size_t> allocation_count_{0};
    std::atomic<size_t> hit_count_{0};
    std::atomic<size_t> miss_count_{0};
    
    // 池同步锁
    std::mutex pool_mutex_;
};

// 自定义FlatBuffers分配器
struct PooledAllocator : public flatbuffers::Allocator {
    MemoryPool& pool;
    
    explicit PooledAllocator(MemoryPool& memory_pool) : pool(memory_pool) {}
    
    virtual uint8_t* allocate(size_t size) override {
        return static_cast<uint8_t*>(pool.allocate(size));
    }
    
    virtual void deallocate(uint8_t* p, size_t size) override {
        pool.deallocate(p, size);
    }
};

class TSDBWriter {
public:
    // 构造函数：打开或创建 mmap 文件，并启动后台处理线程
    explicit TSDBWriter(const std::string& path, 
                        size_t initial_size = 1 << 30, 
                        bool compress = true, 
                        size_t batch_size = 10000,
                        size_t queue_capacity = 100000,
                        size_t merge_interval_ms = 100);

    // 析构函数：确保安全关闭
    ~TSDBWriter();

    // 写入一个时间序列点（线程安全）
    bool write(uint64_t timestamp, double value);
    
    // 批量写入多个时间序列点（线程安全）
    bool write_batch(const std::vector<TimePoint>& points);

    // 手动触发刷新缓冲区
    void flush();

    // 关闭并持久化数据
    void close();

private:
    MMapFile mmap_file_;          // 封装 mmap 操作
    bool compress_;               // 是否启用压缩
    size_t batch_size_;           // 批处理大小
    std::chrono::milliseconds merge_interval_; // 合并间隔
    
    // 内存池和自定义分配器
    MemoryPool memory_pool_;
    PooledAllocator pooled_allocator_{memory_pool_};
    
    std::unique_ptr<boost::lockfree::queue<TimePoint>> point_queue_;
    
    // 用于合并排序的映射
    std::map<uint64_t, double> pending_points_; 
    // std::mutex merge_mutex_;
    
    DeltaDeltaEncoder delta_encoder_;
    std::vector<double> values_;  // 值缓存（用于压缩模式）
    uint64_t min_timestamp_ = 0;  // 当前块最小时间戳
    uint64_t max_timestamp_ = 0;  // 当前块最大时间戳
    
    // 后台处理线程
    std::thread background_thread_;
    std::atomic<bool> running_{true};
    
    // 统计信息
    std::atomic<size_t> points_written_{0};
    std::atomic<size_t> flushes_{0};
    std::atomic<size_t> queue_full_count_{0};
    
    // 后台线程处理函数
    void backgroundProcess();
    
    // 辅助函数：非压缩模式写入单个点
    void writeRaw(uint64_t timestamp, double value);

    // 辅助函数：压缩模式写入时间戳和值
    void writeCompressed(uint64_t timestamp, double value);
    
    // 批处理合并的数据点
    void processBatch(const std::vector<TimePoint>& batch);
    
    // 预分配批处理缓冲区
    std::vector<TimePoint> preallocated_batch_;
};

} // namespace tsdb
