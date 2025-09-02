#include "tsdb/tsdb_writer.h"
#include "tsdb/tsdb_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <boost/align/aligned_allocator.hpp>

namespace tsdb {

// MemoryPool 实现
MemoryPool::MemoryPool() {
    // 分配小块内存
    small_blocks_.resize(SMALL_BLOCKS_COUNT);
    for (auto& block : small_blocks_) {
        block.data = aligned_alloc(64, SMALL_BLOCK_SIZE); // 缓存行对齐
        block.used = false;
    }

    // 分配中块内存
    medium_blocks_.resize(MEDIUM_BLOCKS_COUNT);
    for (auto& block : medium_blocks_) {
        block.data = aligned_alloc(64, MEDIUM_BLOCK_SIZE);
        block.used = false;
    }

    // 分配大块内存
    large_blocks_.resize(LARGE_BLOCKS_COUNT);
    for (auto& block : large_blocks_) {
        block.data = aligned_alloc(64, LARGE_BLOCK_SIZE);
        block.used = false;
    }
}

MemoryPool::~MemoryPool() {
    // 释放所有内存块
    for (auto& block : small_blocks_) {
        free(block.data);
    }
    
    for (auto& block : medium_blocks_) {
        free(block.data);
    }
    
    for (auto& block : large_blocks_) {
        free(block.data);
    }
}

void* MemoryPool::allocate(size_t size) {
    allocation_count_++;
    std::lock_guard<std::mutex> lock(pool_mutex_);

    // 根据请求大小选择合适的内存块
    if (size <= SMALL_BLOCK_SIZE) {
        for (auto& block : small_blocks_) {
            if (!block.used) {
                block.used = true;
                hit_count_++;
                return block.data;
            }
        }
    } 
    else if (size <= MEDIUM_BLOCK_SIZE) {
        for (auto& block : medium_blocks_) {
            if (!block.used) {
                block.used = true;
                hit_count_++;
                return block.data;
            }
        }
    }
    else if (size <= LARGE_BLOCK_SIZE) {
        for (auto& block : large_blocks_) {
            if (!block.used) {
                block.used = true;
                hit_count_++;
                return block.data;
            }
        }
    }

    // 池中没有合适大小的块，回退到标准分配
    miss_count_++;
    return ::malloc(size);
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // 查找并标记为未使用
    auto check_pool = [ptr](std::vector<MemoryBlock>& blocks) -> bool {
        for (auto& block : blocks) {
            if (block.data == ptr) {
                block.used = false;
                return true;
            }
        }
        return false;
    };
    
    // 检查小块池
    if (size <= SMALL_BLOCK_SIZE) {
        if (check_pool(small_blocks_)) return;
    }
    // 检查中块池
    else if (size <= MEDIUM_BLOCK_SIZE) {
        if (check_pool(medium_blocks_)) return;
    }
    // 检查大块池
    else if (size <= LARGE_BLOCK_SIZE) {
        if (check_pool(large_blocks_)) return;
    }
    
    // 不是池中的内存，释放
    ::free(ptr);
}

TSDBWriter::TSDBWriter(const std::string& path, 
                      size_t initial_size, 
                      bool compress,
                      size_t batch_size,
                      size_t queue_capacity,
                      size_t merge_interval_ms)
    : mmap_file_(path, initial_size, /*read_only=*/false), 
      compress_(compress),
      batch_size_(batch_size),
      merge_interval_(std::chrono::milliseconds(merge_interval_ms)) {
          
    // 初始化Boost无锁队列
    size_t capacity = 1;
    while (capacity < queue_capacity) {
        capacity <<= 1;
    }
    
    point_queue_.reset(new boost::lockfree::queue<TimePoint>(capacity));
    
    // 预分配批处理缓冲区
    preallocated_batch_.reserve(batch_size_ * 2);
    values_.reserve(batch_size_ * 2);
    
    // 启动后台处理线程
    background_thread_ = std::thread(&TSDBWriter::backgroundProcess, this);
}

void TSDBWriter::flush() {
    flushes_++;
    
    // 处理所有已排序但未写入的点
    if (!pending_points_.empty()) {
        // 使用预分配的批处理缓冲区
        preallocated_batch_.clear();
        preallocated_batch_.reserve(pending_points_.size());
        
        for (const auto& [ts, val] : pending_points_) {
            preallocated_batch_.push_back({ts, val});
        }
        pending_points_.clear();
        
        // 根据压缩设置处理批次
        for (const auto& point : preallocated_batch_) {
            if (compress_) {
                writeCompressed(point.timestamp, point.value);
            } else {
                writeRaw(point.timestamp, point.value);
            }
        }
    }
    
    // 压缩模式下，确保当前缓冲区的点被写入
    if (compress_ && !values_.empty()) {
        // 使用内存池分配的FlatBufferBuilder
        flatbuffers::FlatBufferBuilder builder(values_.size() * 16, &pooled_allocator_);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(
            reinterpret_cast<const int8_t*>(encoded.data()), 
            encoded.size()
        );
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀
        uint32_t size = builder.GetSize();
        mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
        
        // 写入数据
        mmap_file_.append(builder.GetBufferPointer(), size);
        points_written_ += values_.size();

        // 重置
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
        min_timestamp_ = max_timestamp_ = 0;
    }
}

void TSDBWriter::backgroundProcess() {
    // 使用预分配的缓冲区
    preallocated_batch_.clear();
    preallocated_batch_.reserve(batch_size_);
    
    auto last_flush_time = std::chrono::steady_clock::now();
    
    while (running_) {
        bool batch_ready = false;
        
        // 收集队列中的点
        preallocated_batch_.clear();
        TimePoint point;
        size_t current_count = 0;
        
        // 从队列中批量取出点，最多取到batch_size_
        while (current_count < batch_size_ && point_queue_->pop(point)) {
            preallocated_batch_.push_back(point);
            current_count++;
        }
        
        // 判断是否需要刷新
        auto now = std::chrono::steady_clock::now();
        bool time_to_flush = 
            now - last_flush_time >= merge_interval_ && 
            (!preallocated_batch_.empty() || !pending_points_.empty());
        
        if (!preallocated_batch_.empty() || time_to_flush) {
            // 将从队列取出的点加入待处理映射
            for (const auto& p : preallocated_batch_) {
                pending_points_[p.timestamp] = p.value;
            }
            
            batch_ready = pending_points_.size() >= batch_size_;
            
            // 如果批次足够大或者到了定期刷新时间，处理这批数据
            if (batch_ready || time_to_flush) {
                // 重新使用预分配的批处理缓冲区
                preallocated_batch_.clear();
                preallocated_batch_.reserve(pending_points_.size());
                
                // 将所有点合并为一个有序批次
                for (const auto& [ts, val] : pending_points_) {
                    preallocated_batch_.push_back({ts, val});
                }
                pending_points_.clear();
                
                // 处理批次
                processBatch(preallocated_batch_);
                
                last_flush_time = now;  // 更新最后刷新时间
            }
        } 
        else if (current_count == 0) {
            // 队列为空，短暂休眠减少CPU占用
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void TSDBWriter::writeRaw(uint64_t timestamp, double value) {
    // 使用内存池分配的FlatBufferBuilder
    flatbuffers::FlatBufferBuilder builder(64, &pooled_allocator_);

    auto point = CreateTimeSeriesPoint(builder, timestamp, value);
    builder.Finish(point);

    // 记录当前块的时间戳范围
    if (min_timestamp_ == 0 || timestamp < min_timestamp_) min_timestamp_ = timestamp;
    if (timestamp > max_timestamp_) max_timestamp_ = timestamp;

    // 写入大小前缀
    uint32_t size = builder.GetSize();
    uint64_t offset = mmap_file_.length();
    mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
    
    // 写入数据
    mmap_file_.append(builder.GetBufferPointer(), size);
    points_written_++;

    min_timestamp_ = max_timestamp_ = 0;  // 重置时间戳范围
}

void TSDBWriter::writeCompressed(uint64_t timestamp, double value) {
    delta_encoder_.addTimestamp(timestamp);
    values_.push_back(value);

    // 记录当前块的时间戳范围
    if (min_timestamp_ == 0 || timestamp < min_timestamp_) min_timestamp_ = timestamp;
    if (timestamp > max_timestamp_) max_timestamp_ = timestamp;

    if (values_.size() >= batch_size_) {
        // 使用内存池分配的FlatBufferBuilder
        flatbuffers::FlatBufferBuilder builder(values_.size() * 16, &pooled_allocator_);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(
            reinterpret_cast<const int8_t*>(encoded.data()), 
            encoded.size()
        );
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀
        uint32_t size = builder.GetSize();
        uint64_t offset = mmap_file_.length();
        mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
        
        // 写入数据
        mmap_file_.append(builder.GetBufferPointer(), size);
        points_written_ += values_.size();

        min_timestamp_ = max_timestamp_ = 0;  // 重置时间戳范围

        // 清空缓存但保留容量
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
    }
}

void TSDBWriter::processBatch(const std::vector<TimePoint>& batch) {
    if (batch.empty()) return;
    
    // 处理有序批次
    for (const auto& point : batch) {
        if (compress_) {
            writeCompressed(point.timestamp, point.value);
        } else {
            writeRaw(point.timestamp, point.value);
        }
    }
}

bool TSDBWriter::write(uint64_t timestamp, double value) {
    TimePoint point{timestamp, value};
    
    // 尝试放入队列，如果队列满则进行重试
    int retry_count = 0;
    while (!point_queue_->push(point)) {
        // 队列满，给后台线程一点时间处理
        queue_full_count_++;
        
        if (retry_count++ > 100) {
            // 超过重试次数，主动刷新一次
            flush();
            retry_count = 0;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        
        // 如果系统已关闭，返回失败
        if (!running_) {
            return false;
        }
    }
    
    return true;
}

bool TSDBWriter::write_batch(const std::vector<TimePoint>& points) {
    if (points.empty()) return true;
    
    // // 对于较大的批次，直接进行处理可能比入队更高效
    // if (points.size() > batch_size_ / 2) {
    //     std::lock_guard<std::mutex> lock(merge_mutex_);
    //     for (const auto& point : points) {
    //         pending_points_[point.timestamp] = point.value;
    //     }
    //     return true;
    // }
    
    // 对于小批次，通过队列提交
    for (const auto& point : points) {
        if (!write(point.timestamp, point.value)) {
            return false;
        }
    }
    
    return true;
}

TSDBWriter::~TSDBWriter() {
    close();
}

void TSDBWriter::close() {
    if (running_.exchange(false)) {
        // 等待后台线程完成
        if (background_thread_.joinable()) {
            background_thread_.join();
        }
        
        // 执行最后的刷新
        flush();
    }
}

} // namespace tsdb
