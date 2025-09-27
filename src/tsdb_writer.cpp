#include "tsdb/tsdb_writer.h"
#include "tsdb/tsdb_pch.h"

namespace tsdb {

// MemoryPool 实现
MemoryPool::MemoryPool() {
    // 小块分配器初始化
    initPool(small_blocks_, SMALL_BLOCKS_COUNT, SMALL_BLOCK_SIZE);
    // 中块分配器初始化
    initPool(medium_blocks_, MEDIUM_BLOCKS_COUNT, MEDIUM_BLOCK_SIZE);
    // 大块分配器初始化
    initPool(large_blocks_, LARGE_BLOCKS_COUNT, LARGE_BLOCK_SIZE);
}

MemoryPool::~MemoryPool() {
    // 释放所有内存块
    releasePool(small_blocks_);
    releasePool(medium_blocks_);
    releasePool(large_blocks_);
}

void* MemoryPool::allocate(size_t size) {
    allocation_count_++;
    
    // 根据大小选择分配池
    if (size <= SMALL_BLOCK_SIZE) {
        return allocateFromPool(small_blocks_);
    } 
    else if (size <= MEDIUM_BLOCK_SIZE) {
        return allocateFromPool(medium_blocks_);
    }
    else if (size <= LARGE_BLOCK_SIZE) {
        return allocateFromPool(large_blocks_);
    }
    
    // 过大内存直接分配
    miss_count_++;
    return ::malloc(size);
}

void MemoryPool::deallocate(void* ptr, size_t size) {
    // 根据大小选择释放池
    if (size <= SMALL_BLOCK_SIZE) {
        deallocateFromPool(small_blocks_, ptr);
    }
    else if (size <= MEDIUM_BLOCK_SIZE) {
        deallocateFromPool(medium_blocks_, ptr);
    }
    else if (size <= LARGE_BLOCK_SIZE) {
        deallocateFromPool(large_blocks_, ptr);
    }
    else {
        ::free(ptr);  // 非池内存直接释放
    }
}

// 初始化内存池
void MemoryPool::initPool(MemoryPoolBlock& pool, size_t count, size_t size) {
    // 计算内存池总大小
    size_t total_size = size * count;
    
    // 分配对齐的内存
    pool.base_ptr = aligned_alloc(64, total_size);
    pool.block_size = size;
    pool.block_count = count;
    pool.free_blocks = count;
    
    // 初始化位图 (每64块使用一个uint64管理)
    size_t bitmap_size = (count + BLOCKS_PER_UINT64 - 1) / BLOCKS_PER_UINT64;
    pool.bitmap.resize(bitmap_size, 0);
    
    // 初始化空闲列表
    pool.free_list.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        pool.free_list.push_back(i);
    }
}

// 释放内存池
void MemoryPool::releasePool(MemoryPoolBlock& pool) {
    if (pool.base_ptr) {
        ::free(pool.base_ptr);
        pool.base_ptr = nullptr;
    }
    pool.free_list.clear();
    pool.bitmap.clear();
}

// 从指定池分配内存
void* MemoryPool::allocateFromPool(MemoryPoolBlock& pool) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    if (pool.free_blocks == 0) {
        miss_count_++;
        return nullptr;  // 池已耗尽
    }
    
    // 使用空闲列表快速获取空闲块索引
    size_t block_index = pool.free_list.back();
    pool.free_list.pop_back();
    pool.free_blocks--;
    
    // 设置位图中对应的位
    size_t bitmap_index = block_index / BLOCKS_PER_UINT64;
    size_t bit_offset = block_index % BLOCKS_PER_UINT64;
    pool.bitmap[bitmap_index] |= (1ULL << bit_offset);
    
    // 计算内存地址
    void* ptr = static_cast<uint8_t*>(pool.base_ptr) + block_index * pool.block_size;
    
    hit_count_++;
    return ptr;
}

// 释放内存到指定池
void MemoryPool::deallocateFromPool(MemoryPoolBlock& pool, void* ptr) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // 计算块索引
    auto block_index = (static_cast<uint8_t*>(ptr) - static_cast<uint8_t*>(pool.base_ptr)) / pool.block_size;
    
    // 验证有效性
    if (block_index < 0 || block_index >= pool.block_count) {
        return;  // 无效指针
    }
    
    // 清除位图中对应的位
    size_t bitmap_index = block_index / BLOCKS_PER_UINT64;
    size_t bit_offset = block_index % BLOCKS_PER_UINT64;
    pool.bitmap[bitmap_index] &= ~(1ULL << bit_offset);
    
    // 添加到空闲列表
    pool.free_list.push_back(block_index);
    pool.free_blocks++;
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
                      size_t shard_count,
                      bool enable_monitoring)
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
            initial_size / shard_count_,
            compress, 
            batch_size));
    }
    
    // 启动分片处理线程
    shard_threads_.resize(shard_count_);
    for (size_t i = 0; i < shard_count_; ++i) {
        shard_threads_[i] = std::thread(&TSDBWriter::shardProcessThread, this, i);
    }
    // recoverFromWAL();
    
    // 初始化性能监控器（如果启用）
    if (enable_monitoring) {
        monitor_ = std::make_unique<PerformanceMonitor>(this);
        monitor_->start();
    }
}

TSDBWriter::~TSDBWriter() {
    if (monitor_) {
        monitor_->stop();
    }

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

// // 获取队列大小
// std::vector<size_t> TSDBWriter::getQueueSizes() const {
//     std::vector<size_t> sizes(shard_count_);
//     for (size_t i = 0; i < shard_count_; ++i) {
//         sizes[i] = shard_queues_[i]->read_available();
//     }
//     return sizes;
// }

// 获取分片写入点数
std::vector<size_t> TSDBWriter::getShardPointsWritten() const {
    std::vector<size_t> points(shard_count_);
    for (size_t i = 0; i < shard_count_; ++i) {
        points[i] = shard_writers_[i]->getPointsWritten();
    }
    return points;
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

// PerformanceMonitor 实现
PerformanceMonitor::PerformanceMonitor(TSDBWriter* writer)
    : writer_(writer) {
    // 初始化上次指标 - 确保向量大小正确
    last_metrics_.timestamp = std::chrono::steady_clock::now();
    last_metrics_.points_written = 0;
    last_metrics_.queue_full_count = 0;
    last_metrics_.flushes = 0;
    last_metrics_.shard_points.resize(writer_->getShardCount(), 0);
    
    // 立即获取当前状态作为基线
    last_metrics_.points_written = writer_->getTotalPointsWritten();
    last_metrics_.queue_full_count = writer_->getQueueFullCount();
    last_metrics_.flushes = writer_->getFlushCount();
    last_metrics_.shard_points = writer_->getShardPointsWritten();
}

PerformanceMonitor::~PerformanceMonitor() {
    stop();
}

void PerformanceMonitor::start() {
    if (running_) return;
    
    running_ = true;
    
    // 启动监控线程
    monitor_thread_ = std::thread(&PerformanceMonitor::monitorThread, this);
}

void PerformanceMonitor::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void PerformanceMonitor::monitorThread() {
    // 等待第一次数据收集
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    while (running_) {
        // 获取当前指标
        Metrics current;
        current.timestamp = std::chrono::steady_clock::now();
        current.points_written = writer_->getTotalPointsWritten();
        current.queue_full_count = writer_->getQueueFullCount();
        current.flushes = writer_->getFlushCount();
        current.shard_points = writer_->getShardPointsWritten();
        
        // 计算时间差（秒）
        auto time_diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            current.timestamp - last_metrics_.timestamp).count();
        
        double time_diff = time_diff_ms / 1000.0;
        
        // 避免除零错误
        if (time_diff <= 0.0) {
            time_diff = 1.0;  // 设置最小时间差为1秒
        }
        
        // 计算写入速率（点/秒）
        double write_rate = 0.0;
        if (current.points_written >= last_metrics_.points_written) {
            write_rate = (current.points_written - last_metrics_.points_written) / time_diff;
        }
        
        // 计算队列满增长率
        double queue_full_rate = 0.0;
        if (current.queue_full_count >= last_metrics_.queue_full_count) {
            queue_full_rate = (current.queue_full_count - last_metrics_.queue_full_count) / time_diff;
        }
        
        // 计算刷新率
        double flush_rate = 0.0;
        if (current.flushes >= last_metrics_.flushes) {
            flush_rate = (current.flushes - last_metrics_.flushes) / time_diff;
        }
        
        // 清屏并显示信息
        std::cout << "\033[2J\033[1;1H";  // ANSI 清屏和光标定位到开始
        
        // 显示标题
        std::cout << "TSDB Writer Performance Monitor" << std::endl;
        std::cout << "===========================================================" << std::endl;
        std::cout << "Time: " << time_diff_ms << "ms since last update" << std::endl;
        std::cout << std::endl;
        
        // 显示全局指标
        std::cout << "Total Points Written: " << current.points_written 
                  << " (" << std::fixed << std::setprecision(2) << write_rate << " points/s)" << std::endl;
                  
        // std::cout << "Queue Full Count: " << current.queue_full_count 
        //           << " (" << std::fixed << std::setprecision(2) << queue_full_rate << " /s)" << std::endl;
                  
        // std::cout << "Flush Count: " << current.flushes 
        //           << " (" << std::fixed << std::setprecision(2) << flush_rate << " /s)" << std::endl;
        
        // 显示分片指标
        std::cout << std::endl << "Shard Statistics:" << std::endl;
        std::cout << "-------------------------------------------------------" << std::endl;
        std::cout << "Shard ID | Points Written | Write Rate" << std::endl;
        std::cout << "-------------------------------------------------------" << std::endl;
        
        for (size_t i = 0; i < writer_->getShardCount(); ++i) {
            double shard_rate = 0.0;
            if (i < current.shard_points.size() && i < last_metrics_.shard_points.size()) {
                if (current.shard_points[i] >= last_metrics_.shard_points[i]) {
                    shard_rate = (current.shard_points[i] - last_metrics_.shard_points[i]) / time_diff;
                }
            }
            
            size_t shard_points = (i < current.shard_points.size()) ? current.shard_points[i] : 0;
            
            std::cout << std::setw(8) << i << " | " 
                      << std::setw(14) << shard_points << " | " 
                      << std::setw(10) << std::fixed << std::setprecision(2) << shard_rate << " /s" << std::endl;
        }
        
        // 显示帮助信息
        std::cout << std::endl << "Press Ctrl+C to exit" << std::endl;
        std::cout << "Time diff: " << std::fixed << std::setprecision(3) << time_diff << "s" << std::endl;
        
        // 保存当前指标作为下次计算基础
        last_metrics_ = current;
        
        // 等待1秒
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace tsdb
