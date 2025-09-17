#include "tsdb/tsdb_writer.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <boost/align/aligned_allocator.hpp>
#include <sstream>
#include <filesystem>

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

// ShardWriter实现
ShardWriter::ShardWriter(const std::string& path, size_t initial_size, bool compress, size_t batch_size)
    : mmap_file_(path, initial_size, /*read_only=*/false), 
      compress_(compress),
      batch_size_(batch_size) {
    values_.reserve(batch_size * 2);
}

ShardWriter::~ShardWriter() {
    close();
}

void ShardWriter::write(uint64_t timestamp, double value) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (compress_) {
        writeCompressed(timestamp, value);
    } else {
        writeRaw(timestamp, value);
    }
}

void ShardWriter::writeBatch(const std::vector<TimePoint>& points) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    for (const auto& point : points) {
        if (compress_) {
            writeCompressed(point.timestamp, point.value);
        } else {
            writeRaw(point.timestamp, point.value);
        }
    }
}

void ShardWriter::writeRaw(uint64_t timestamp, double value) {
    flatbuffers::FlatBufferBuilder builder(64, &pooled_allocator_);
    auto point = CreateTimeSeriesPoint(builder, timestamp, value);
    builder.Finish(point);

    // 记录时间戳范围
    if (min_timestamp_ == 0 || timestamp < min_timestamp_) min_timestamp_ = timestamp;
    if (timestamp > max_timestamp_) max_timestamp_ = timestamp;

    // 写入大小前缀和数据
    uint32_t size = builder.GetSize();
    mmap_file_.append(&size, sizeof(size));
    mmap_file_.append(builder.GetBufferPointer(), size);
    points_written_++;

    min_timestamp_ = max_timestamp_ = 0;  // 重置时间戳范围
}

void ShardWriter::writeCompressed(uint64_t timestamp, double value) {
    delta_encoder_.addTimestamp(timestamp);
    values_.push_back(value);

    // 记录时间戳范围
    if (min_timestamp_ == 0 || timestamp < min_timestamp_) min_timestamp_ = timestamp;
    if (timestamp > max_timestamp_) max_timestamp_ = timestamp;

    if (values_.size() >= batch_size_) {
        flatbuffers::FlatBufferBuilder builder(values_.size() * 16, &pooled_allocator_);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(
            reinterpret_cast<const int8_t*>(encoded.data()), 
            encoded.size()
        );
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀和数据
        uint32_t size = builder.GetSize();
        mmap_file_.append(&size, sizeof(size));
        mmap_file_.append(builder.GetBufferPointer(), size);
        points_written_ += values_.size();

        // 重置状态
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
        min_timestamp_ = max_timestamp_ = 0;
    }
}

void ShardWriter::flush() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    
    // 确保所有压缩数据都被写入
    if (compress_ && !values_.empty()) {
        flatbuffers::FlatBufferBuilder builder(values_.size() * 16, &pooled_allocator_);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(
            reinterpret_cast<const int8_t*>(encoded.data()), 
            encoded.size()
        );
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀和数据
        uint32_t size = builder.GetSize();
        mmap_file_.append(&size, sizeof(size));
        mmap_file_.append(builder.GetBufferPointer(), size);
        points_written_ += values_.size();

        // 重置状态
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
        min_timestamp_ = max_timestamp_ = 0;
    }
}

void ShardWriter::close() {
    flush();
}

// TSDBWriter 实现
TSDBWriter::TSDBWriter(const std::string& path, 
                      size_t initial_size, 
                      bool compress,
                      size_t batch_size,
                      size_t queue_capacity,
                      size_t merge_interval_ms,
                      size_t shard_count)
    : base_path_(path),
      compress_(compress),
      batch_size_(batch_size),
      shard_count_(shard_count),
      merge_interval_(std::chrono::milliseconds(merge_interval_ms)) {

    wal_.reset(new WALWriter(path, initial_size));
    
    // 确保基础目录存在
    std::filesystem::path dir_path(path);
    dir_path = dir_path.parent_path();
    if (!dir_path.empty() && !std::filesystem::exists(dir_path)) {
        std::filesystem::create_directories(dir_path);
    }
    
    // 初始化每个分片的队列和写入器
    shard_queues_.resize(shard_count_);
    shard_writers_.resize(shard_count_);
    
    // 计算队列容量 (2的幂次)
    size_t capacity = 1;
    while (capacity < queue_capacity / shard_count_) {
        capacity <<= 1;
    }
    
    // 创建分片队列和写入器
    for (size_t i = 0; i < shard_count_; ++i) {
        // 创建分片队列
        shard_queues_[i].reset(new boost::lockfree::queue<TimePoint>(capacity));
        
        // 为每个分片创建文件路径
        std::ostringstream shard_path;
        shard_path << base_path_ << ".shard" << i;
        
        // 创建分片写入器
        shard_writers_[i].reset(new ShardWriter(
            shard_path.str(), 
            initial_size / shard_count_,  // 均分初始大小
            compress, 
            batch_size));
    }
    
    // 启动分片处理线程
    shard_threads_.resize(shard_count_);
    for (size_t i = 0; i < shard_count_; ++i) {
        shard_threads_[i] = std::thread(&TSDBWriter::shardProcessThread, this, i);
    }

    // recoverFromWAL();
}

TSDBWriter::~TSDBWriter() {
    close();
}

void TSDBWriter::recoverFromWAL() {
    size_t recovered = wal_->recover([this](uint32_t shard_id, uint64_t timestamp, double value) {
        if (shard_id < shard_count_) {
            // 直接写入对应分片，绕过队列
            shard_writers_[shard_id]->write(timestamp, value);
        }
    });
    
    if (recovered > 0) {
        std::cout << "Recovered " << recovered << " points from WAL" << std::endl;
    }
}

bool TSDBWriter::write(uint64_t timestamp, double value) {
    // 计算分片索引
    uint32_t shard_idx = getShardIndex(timestamp);

    // 先写WAL
    wal_->logPoint(shard_idx, timestamp, value);
    
    // 创建数据点
    TimePoint point{timestamp, value};
    
    // 尝试放入对应分片的队列
    int retry_count = 0;
    while (!shard_queues_[shard_idx]->push(point)) {
        // 队列满，等待处理
        queue_full_count_++;
        
        if (retry_count++ > 100) {
            // 超过重试次数，主动刷新该分片
            shard_writers_[shard_idx]->flush();
            retry_count = 0;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        
        if (!running_) {
            return false;
        }
    }
    
    return true;
}

bool TSDBWriter::write_batch(const std::vector<TimePoint>& points) {
    if (points.empty()) return true;
    
    // 按分片对点进行分组
    std::vector<std::vector<TimePoint>> shard_points(shard_count_);
    for (const auto& point : points) {
        uint32_t shard_idx = getShardIndex(point.timestamp);
        shard_points[shard_idx].push_back(point);
    }
    
    // 处理每个分片的点
    for (size_t i = 0; i < shard_count_; ++i) {
        if (shard_points[i].empty()) continue;
        
        // 先写WAL
        wal_->logBatch(i, shard_points[i]);

        // 大批量直接写入，小批量放入队列
        if (shard_points[i].size() > batch_size_ / 2) {
            shard_writers_[i]->writeBatch(shard_points[i]);
            points_written_ += shard_points[i].size();

            wal_->markProcessed(i, wal_->getCurrentPosition());

        } else {
            for (const auto& point : shard_points[i]) {
                int retry_count = 0;
                while (!shard_queues_[i]->push(point)) {
                    queue_full_count_++;
                    
                    if (retry_count++ > 100) {
                        shard_writers_[i]->flush();
                        retry_count = 0;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    
                    if (!running_) {
                        return false;
                    }
                }
            }
        }
    }
    
    return true;
}

void TSDBWriter::shardProcessThread(size_t shard_index) {
    // 预分配批处理缓冲区
    std::vector<TimePoint> batch;
    batch.reserve(batch_size_ * 2);
    
    // 记录上次刷新时间
    auto last_flush_time = std::chrono::steady_clock::now();
    
    // 映射用于对每个批次内的点进行排序
    std::map<uint64_t, double> pending_points;
    
    while (running_) {
        bool batch_ready = false;
        
        // 从队列中批量取出点
        batch.clear();
        TimePoint point;
        size_t count = 0;
        
        while (count < batch_size_ && shard_queues_[shard_index]->pop(point)) {
            batch.push_back(point);
            count++;
        }
        
        // 检查是否需要刷新
        auto now = std::chrono::steady_clock::now();
        bool time_to_flush = (now - last_flush_time >= merge_interval_) && 
                            (!batch.empty() || !pending_points.empty());
        
        if (!batch.empty()) {
            // 将新点加入排序映射
            for (const auto& p : batch) {
                pending_points[p.timestamp] = p.value;
            }
            
            batch_ready = pending_points.size() >= batch_size_;
        }
        
        // 处理批次
        if (batch_ready || time_to_flush) {
            if (!pending_points.empty()) {
                // 准备有序批次
                batch.clear();
                for (const auto& [ts, val] : pending_points) {
                    batch.push_back({ts, val});
                }
                pending_points.clear();
                
                // 写入批次
                processShardBatch(shard_index, batch);
                last_flush_time = now;
            }
        } 
        else if (count == 0) {
            // 队列为空，短暂休眠减少CPU占用
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void TSDBWriter::processShardBatch(size_t shard_index, const std::vector<TimePoint>& batch) {
    if (batch.empty()) return;
    
    // 先获取当前WAL位置
    uint64_t current_wal_pos = wal_->getCurrentPosition();
    
    // 写入分片
    shard_writers_[shard_index]->writeBatch(batch);
    
    // 等待分片写入完成后再标记WAL处理进度
    shard_writers_[shard_index]->flush();
    
    // 使用实际的WAL位置
    wal_->markProcessed(shard_index, current_wal_pos);
    
    points_written_ += batch.size();
}

void TSDBWriter::flush() {
    // 刷新所有分片写入器
    for (auto& writer : shard_writers_) {
        writer->flush();
    }
    flushes_++;
}

void TSDBWriter::close() {
    if (running_.exchange(false)) {
        // 等待所有分片线程完成
        for (auto& thread : shard_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // 刷新并关闭所有分片写入器
        for (auto& writer : shard_writers_) {
            writer->flush();
            writer->close();
        }

        // 关闭WAL
        wal_->close();
    }
}

} // namespace tsdb
