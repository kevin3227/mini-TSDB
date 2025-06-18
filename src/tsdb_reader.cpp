#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <iostream>
#include <algorithm>

namespace tsdb {

TSDBReader::TSDBReader(const std::string& path) 
    : mmap_file_(path, 0, true) {  // 以只读方式打开
    data_ = static_cast<const uint8_t*>(mmap_file_.data());
    end_ = data_ + mmap_file_.length();
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
        
        // 检查是否为索引块
        auto point = flatbuffers::GetRoot<TimeSeriesPoint>(block_data);
        if (point->Verify(verifier)) {
            uint64_t timestamp = point->timestamp();
            uint64_t data_offset = static_cast<uint64_t>(point->value());
            
            // 这是个索引块
            time_index_[timestamp] = data_offset;
            is_index = true;
            min_ts = timestamp;
        } else if (VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
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
            .size = block_size,
            .is_index = is_index
        };
        
        // 只索引非索引块
        if (!is_index) {
            time_blocks_[min_ts] = info;
        }
        offset_to_block_[block_offset] = info;
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

std::vector<std::pair<uint64_t, double>> TSDBReader::query(uint64_t start, uint64_t end) {
    std::vector<std::pair<uint64_t, double>> results;
    
    // 加载索引（线程安全）
    loadIndex();
    
    // 使用索引查找范围内的数据块
    auto it = time_blocks_.lower_bound(start);
    
    // 如果没有找到大于等于start的时间戳，从头开始查找第一个块
    if (it == time_blocks_.end() && !time_blocks_.empty()) {
        it = time_blocks_.begin();
    }
    
    // 收集需要处理的块
    std::vector<BlockInfo> blocks_to_process;
    
    // 处理数据块
    while (it != time_blocks_.end()) {
        const BlockInfo& info = it->second;
        
        // 处理数据块
        const uint8_t* block_data = data_ + info.offset + sizeof(uint32_t); // 跳过大小前缀
        processBlock(block_data, info.size, start, end, results);
        
        ++it;
    }
    
    // 按时间戳排序结果
    std::sort(results.begin(), results.end(), 
              [](const auto& a, const auto& b) { return a.first < b.first; });
    
    return results;
}

} // namespace tsdb
