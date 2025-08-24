#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <iostream>
#include <algorithm>

namespace tsdb {

TSDBReader::TSDBReader(const std::string& path, size_t cache_capacity) 
    : mmap_file_(path, 0, true),  // 以只读方式打开
      cache_capacity_(cache_capacity) {
    data_ = static_cast<const uint8_t*>(mmap_file_.data());
    end_ = data_ + mmap_file_.length();
}

TSDBReader::~TSDBReader() {
    clearCache();
}

void TSDBReader::clearCache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_map_.clear();
    cache_list_.clear();
    cache_hits_ = 0;
    cache_misses_ = 0;
}

const uint8_t* TSDBReader::readNextBlock(const uint8_t* current_pos, uint32_t* out_size) {
    if (current_pos + sizeof(uint32_t) > end_) {
        return nullptr;  // 没有足够的数据读取大小前缀
    }
    
    // 读取大小前缀
    *out_size = *reinterpret_cast<const uint32_t*>(current_pos);
    current_pos += sizeof(uint32_t);
    
    if (current_pos + *out_size > end_) {
        throw std::runtime_error("Invalid block size in data file");
    }
    
    return current_pos;
}

void TSDBReader::loadIndex() {
    std::lock_guard<std::mutex> lock(index_mutex_);
    if (index_loaded_) return;
    
    const uint8_t* current_pos = data_;
    uint64_t current_offset = 0;
    
    // 第一遍扫描：识别所有数据块并收集块信息
    while (current_pos < end_) {
        uint32_t block_size = 0;
        const uint8_t* block_data = readNextBlock(current_pos, &block_size);
        if (!block_data) break;
        
        // 块的文件偏移位置
        uint64_t block_offset = current_pos - data_;
        current_pos = block_data + block_size;  // 移动到下一个块
        
        // 验证并解析块内容
        flatbuffers::Verifier verifier(block_data, block_size);
        bool is_index = false;
        uint64_t min_ts = 0;
        
        auto point = flatbuffers::GetRoot<TimeSeriesPoint>(block_data);
        if (VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
            // 这是压缩数据块，尝试提取最小时间戳
            auto segment = GetCompressedTimeSeriesSegment(block_data);
            
            // 需要解码前几个时间戳以获取最小值
            DeltaDeltaDecoder decoder(
                reinterpret_cast<const uint8_t*>(segment->metadata()->data()),
                segment->metadata()->size());
            
            if (decoder.next(&min_ts)) {
                // 找到了最小时间戳
            }
        } else {
            // 尝试当作单点处理
            if (point->Verify(verifier)) {
                min_ts = point->timestamp();
            }
        }
        
        // 记录块信息
        BlockInfo info = {
            .min_ts = min_ts,
            .offset = block_offset,
            .size = block_size
        };
        
        time_blocks_[min_ts] = info;
    }
    
    index_loaded_ = true;
}

void TSDBReader::processBlock(
    const uint8_t* block_data, 
    uint32_t block_size, 
    uint64_t start, 
    uint64_t end, 
    std::vector<std::pair<uint64_t, double>>& results) {
    
    flatbuffers::Verifier verifier(block_data, block_size);
    
    // 处理压缩数据块
    if (VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
        auto segment = GetCompressedTimeSeriesSegment(block_data);
        
        // 解码时间戳
        DeltaDeltaDecoder decoder(
            reinterpret_cast<const uint8_t*>(segment->metadata()->data()),
            segment->metadata()->size());
        
        // 获取值数组
        auto values = segment->values();
        if (!values) return;
        
        // 遍历解码时间戳并匹配值
        uint64_t timestamp;
        size_t value_index = 0;
        while (decoder.next(&timestamp)) {
            if (timestamp >= start && timestamp <= end) {
                if (value_index < values->size()) {
                    results.emplace_back(timestamp, values->Get(value_index));
                }
            } else if (timestamp > end) {
                break;  // 时间戳已超过查询范围，提前终止
            }
            value_index++;
        }
    } else {
        // 尝试作为原始数据点解析
        auto point = flatbuffers::GetRoot<TimeSeriesPoint>(block_data);
        if (point->Verify(verifier)) {
            uint64_t timestamp = point->timestamp();
            if (timestamp >= start && timestamp <= end) {
                results.emplace_back(timestamp, point->value());
            }
        }
    }
}

void TSDBReader::loadIndexLazy(uint64_t start_ts, uint64_t end_ts) {
    std::lock_guard<std::mutex> lock(index_mutex_);
    
    // 检查是否需要加载索引
    if (index_status_.fully_loaded) {
        return; // 索引已完全加载
    }
    
    // 检查请求范围是否已在加载范围内
    if (start_ts >= index_status_.min_loaded_ts && 
        end_ts <= index_status_.max_loaded_ts && 
        !time_blocks_.empty()) {
        return; // 请求范围的索引已加载
    }
    
    // 扩展加载范围，包括前后一定的余量
    uint64_t load_start = (start_ts > 1000000) ? start_ts - 1000000 : 0;
    uint64_t load_end = end_ts + 1000000;
    
    // 扫描文件，仅加载相关范围的索引
    const uint8_t* current_pos = data_;
    while (current_pos < end_) {
        uint32_t block_size = 0;
        const uint8_t* block_data = readNextBlock(current_pos, &block_size);
        if (!block_data) break;
        
        uint64_t block_offset = current_pos - data_;
        current_pos = block_data + block_size;
        
        // 如果该块已被索引，跳过
        if (time_blocks_.find(block_offset) != time_blocks_.end()) {
            continue;
        }
        
        // 验证并解析块内容
        flatbuffers::Verifier verifier(block_data, block_size);
        bool is_index = false;
        uint64_t min_ts = 0;
        
        auto point = flatbuffers::GetRoot<TimeSeriesPoint>(block_data);
        if (VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
            // 压缩数据块，提取最小时间戳
            auto segment = GetCompressedTimeSeriesSegment(block_data);
            DeltaDeltaDecoder decoder(
                reinterpret_cast<const uint8_t*>(segment->metadata()->data()),
                segment->metadata()->size());
            
            if (decoder.next(&min_ts)) {
                // 如果时间戳不在我们关心的范围内，可以跳过
                if (min_ts > load_end || min_ts < load_start) {
                    continue;
                }
            }
        } else {
            // 尝试作为单点处理
            if (point->Verify(verifier)) {
                min_ts = point->timestamp();
                // 如果时间戳不在我们关心的范围内，跳过
                if (min_ts > load_end || min_ts < load_start) {
                    continue;
                }
            }
        }
        
        // 记录块信息
        BlockInfo info = {
            .min_ts = min_ts,
            .offset = block_offset,
            .size = block_size
        };
    
        time_blocks_[min_ts] = info;
    }
    
    // 更新已加载的范围
    if (load_start < index_status_.min_loaded_ts) 
        index_status_.min_loaded_ts = load_start;
    if (load_end > index_status_.max_loaded_ts) 
        index_status_.max_loaded_ts = load_end;
    
    // 检查是否已完整加载所有索引
    if (index_status_.min_loaded_ts == 0 && 
        current_pos >= end_) {
        index_status_.fully_loaded = true;
    }
}

// 查询LRU缓存
bool TSDBReader::lookupCache(uint64_t start, uint64_t end, 
                           std::vector<std::pair<uint64_t, double>>& result) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // 构建查询键
    TimeRange range{start, end};
    
    // 查找精确匹配
    auto it = cache_map_.find(range);
    if (it != cache_map_.end()) {
        // 缓存命中，将节点移到链表前端
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        
        // 返回缓存的结果
        result = it->second->data;
        cache_hits_++;
        return true;
    }
    
    cache_misses_++;
    return false;
}

// 更新LRU缓存
void TSDBReader::updateCache(uint64_t start, uint64_t end,
                          const std::vector<std::pair<uint64_t, double>>& data) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // 构建缓存键
    TimeRange range{start, end};
    
    // 检查是否已存在
    auto it = cache_map_.find(range);
    if (it != cache_map_.end()) {
        // 存在则更新数据并移到链表前端
        it->second->data = data;
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        return;
    }
    
    // 如果缓存已满，移除最后一个元素（最久未使用的）
    if (cache_list_.size() >= cache_capacity_) {
        // 获取最后一个节点的键
        const TimeRange& last_range = cache_list_.back().range;
        // 从映射中删除
        cache_map_.erase(last_range);
        // 从链表中删除
        cache_list_.pop_back();
    }
    
    // 在链表前端添加新节点
    cache_list_.emplace_front(LRUCacheNode{range, data});
    // 更新映射
    cache_map_[range] = cache_list_.begin();
}

std::vector<std::pair<uint64_t, double>> TSDBReader::query(uint64_t start, uint64_t end) {
    std::vector<std::pair<uint64_t, double>> results;
    
    // 1. 尝试从缓存获取
    if (lookupCache(start, end, results)) {
        return results;  // 缓存命中
    }
    
    // 2. 缓存未命中，执行原有查询逻辑
    
    // 按需加载索引
    loadIndexLazy(start, end);
    
    // 使用索引查找范围内的数据块
    auto it = time_blocks_.lower_bound(start);
    
    // 如果查询范围很小，我们可能直接处理范围内的块
    while (it != time_blocks_.end() && it->first <= end) {
        const BlockInfo& info = it->second;
        
        // 处理数据块
        const uint8_t* block_data = data_ + info.offset + sizeof(uint32_t); // 跳过大小前缀
        processBlock(block_data, info.size, start, end, results);
        
        ++it;
    }
    
    // 按时间戳排序结果
    std::sort(results.begin(), results.end(), 
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    // 3. 将查询结果更新到缓存
    updateCache(start, end, results);
    
    return results;
}

} // namespace tsdb
