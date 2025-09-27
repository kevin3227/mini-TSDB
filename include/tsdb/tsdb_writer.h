#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"
#include "tsdb/wal.h"
#include "tsdb/tsdb_generated.h"

#include <vector>
#include <string>
#include <flatbuffers/flatbuffers.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <boost/lockfree/queue.hpp>
#include <filesystem>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>

namespace tsdb {

// 自定义内存分配器
class MemoryPool {
public:
    static constexpr size_t BITS_PER_BLOCK = 64;
    static constexpr size_t BLOCKS_PER_UINT64 = sizeof(uint64_t) * 8;

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
    struct MemoryPoolBlock {
        void* base_ptr = nullptr;    // 整个内存池起始地址
        size_t block_size = 0;        // 每个块的大小
        size_t block_count = 0;        // 总块数
        size_t free_blocks = 0;        // 空闲块数
        
        // 位图管理
        std::vector<uint64_t> bitmap;
        
        // 空闲块索引数组
        std::vector<size_t> free_list;
    };

    MemoryPoolBlock small_blocks_;   // 小块内存池
    MemoryPoolBlock medium_blocks_;  // 中块内存池
    MemoryPoolBlock large_blocks_;   // 大块内存池
    
    void initPool(MemoryPoolBlock& pool, size_t count, size_t size);
    void releasePool(MemoryPoolBlock& pool);
    void* allocateFromPool(MemoryPoolBlock& pool);
    void deallocateFromPool(MemoryPoolBlock& pool, void* ptr);

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

// 单分片写入器
class ShardWriter {
public:
    ShardWriter(const std::string& path, size_t initial_size, bool compress, size_t batch_size);
    ~ShardWriter();
    
    // 写入单个点
    void write(uint64_t timestamp, double value);
    
    // 批量写入点
    void writeBatch(const std::vector<TimePoint>& points);
    
    // 刷新缓冲区
    void flush();
    
    // 关闭写入器
    void close();
    
    // 获取统计信息
    size_t getPointsWritten() const { return points_written_; }
    
private:
    MMapFile mmap_file_;
    bool compress_;
    size_t batch_size_;
    
    MemoryPool memory_pool_;
    PooledAllocator pooled_allocator_{memory_pool_};
    
    DeltaDeltaEncoder delta_encoder_;
    std::vector<double> values_;
    uint64_t min_timestamp_ = 0;
    uint64_t max_timestamp_ = 0;
    std::mutex write_mutex_; // 保护分片内的写入
    std::atomic<size_t> points_written_{0};
    
    void writeRaw(uint64_t timestamp, double value);
    void writeCompressed(uint64_t timestamp, double value);
};

// 前向声明
class PerformanceMonitor;

class TSDBWriter {
public:
    // 构造函数：打开或创建 mmap 文件，并启动后台处理线程
    explicit TSDBWriter(const std::string& path, 
                        size_t initial_size = 1 << 30, 
                        bool compress = true, 
                        size_t batch_size = 10000,
                        size_t queue_capacity = 100000,
                        size_t merge_interval_ms = 100,
                        size_t shard_count = 8,
                        bool enable_monitoring = true);

    // 析构函数：确保安全关闭
    ~TSDBWriter();

    // 写入一个时间序列点
    bool write(uint64_t timestamp, double value);
    
    // 批量写入多个时间序列点
    bool write_batch(const std::vector<TimePoint>& points);

    // 手动触发刷新缓冲区
    void flush();

    // 关闭并持久化数据
    void close();
    
    // 获取队列大小
    // std::vector<size_t> getQueueSizes() const;
    
    // 获取分片写入点数
    std::vector<size_t> getShardPointsWritten() const;
    
    // 获取写入点总数
    size_t getTotalPointsWritten() const { return points_written_; }
    
    // 获取队列满计数
    size_t getQueueFullCount() const { return queue_full_count_; }
    
    // 获取刷新次数
    size_t getFlushCount() const { return flushes_; }
    
    // 获取分片数
    size_t getShardCount() const { return shard_count_; }

private:
    std::string base_path_; // 基本路径，用于构造分片文件路径
    bool compress_;         // 是否启用压缩
    size_t batch_size_;     // 批处理大小
    size_t shard_count_;    // 分片数量
    std::chrono::milliseconds merge_interval_; // 合并间隔

    std::unique_ptr<WALWriter> wal_; // WAL写入器
    
    // 分片哈希函数
    uint32_t getShardIndex(uint64_t timestamp) const {
        return timestamp % shard_count_;
    }
    
    // 分片写入器
    std::vector<std::unique_ptr<ShardWriter>> shard_writers_;
    
    // 每个分片一个队列
    std::vector<std::unique_ptr<boost::lockfree::queue<TimePoint>>> shard_queues_;
    
    // 每个分片一个处理线程
    std::vector<std::thread> shard_threads_;
    
    // 统计信息
    std::atomic<size_t> points_written_{0};
    std::atomic<size_t> flushes_{0};
    std::atomic<size_t> queue_full_count_{0};
    
    // 运行标志
    std::atomic<bool> running_{true};
    
    // 性能监控器
    std::unique_ptr<PerformanceMonitor> monitor_;
    
    // 分片处理线程主函数
    void shardProcessThread(size_t shard_index);
    
    // 分片批处理函数
    void processShardBatch(size_t shard_index, const std::vector<TimePoint>& batch);

    // 从WAL恢复数据
    void recoverFromWAL();
};

// 性能监控类 - 在TSDBWriter定义之后
class PerformanceMonitor {
public:
    // 构造函数
    explicit PerformanceMonitor(TSDBWriter* writer);
    
    // 析构函数
    ~PerformanceMonitor();
    
    // 启动监控线程
    void start();
    
    // 停止监控线程
    void stop();
    
private:
    // 监控线程主函数
    void monitorThread();
    
    // 性能指标
    struct Metrics {
        std::chrono::time_point<std::chrono::steady_clock> timestamp;
        size_t points_written;
        size_t queue_full_count;
        size_t flushes;
        // std::vector<size_t> queue_sizes;
        std::vector<size_t> shard_points;
    };
    
    TSDBWriter* writer_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    
    // 缓存上次的指标，用于计算速率
    Metrics last_metrics_;
};

} // namespace tsdb
