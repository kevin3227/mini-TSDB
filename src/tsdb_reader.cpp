#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_generated.h"  // FlatBuffers生成的头文件
#include <flatbuffers/flatbuffers.h>
#include <iostream>

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

std::vector<std::pair<uint64_t, double>> TSDBReader::query(uint64_t start, uint64_t end) {
    std::vector<std::pair<uint64_t, double>> results;
    
    const uint8_t* current_pos = data_;
    while (current_pos < end_) {
        uint32_t block_size = 0;
        const uint8_t* block_data = readNextBlock(current_pos, &block_size);
        if (!block_data) break;
        
        // 验证FlatBuffer数据完整性
        flatbuffers::Verifier verifier(block_data, block_size);
        
        // 优先检查是否为压缩数据段
        if (VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
            // 处理压缩数据块
            auto segment = GetCompressedTimeSeriesSegment(block_data);
            
            // 解码时间戳
            DeltaDeltaDecoder decoder(
                reinterpret_cast<const uint8_t*>(segment->metadata()->data()),
                segment->metadata()->size());
            
            // 获取值数组
            auto values = segment->values();
            if (!values) continue;
            
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
            uint64_t timestamp = point->timestamp();
            if (timestamp >= start && timestamp <= end) {
                results.emplace_back(timestamp, point->value());
            }
        }
        
        current_pos = block_data + block_size;
    }
    
    return results;
}

} // namespace tsdb
