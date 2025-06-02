#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_generated.h"  // FlatBuffers生成的头文件
#include <flatbuffers/flatbuffers.h>
#include <iostream>

namespace tsdb {

TSDBReader::TSDBReader(const std::string& path) 
    : mmap_file_(path, 0, true) {  // 以只读方式打开
    data_ = mmap_file_.data();
    size_ = mmap_file_.length();
}

std::vector<std::pair<uint64_t, double>> TSDBReader::query(uint64_t start, uint64_t end) {
    std::vector<std::pair<uint64_t, double>> results;
    
    // 1. 验证FlatBuffer数据完整性
    flatbuffers::Verifier verifier(data_, size_);
    if (!VerifyCompressedTimeSeriesSegmentBuffer(verifier)) {
        throw std::runtime_error("Invalid FlatBuffer data");
    }

    // 2. 获取根对象
    auto segment = GetCompressedTimeSeriesSegment(data_);
    
    // 3. 解码时间戳
    DeltaDeltaDecoder decoder(reinterpret_cast<const uint8_t*>(segment->metadata()->data()), 
                             segment->metadata()->size());
    
    // 4. 获取值数组
    auto values = segment->values();
    if (values == nullptr) {
        return results;
    }

    // 5. 遍历解码时间戳并匹配值
    uint64_t timestamp;
    size_t value_index = 0;
    while (decoder.next(&timestamp)) {
        if (timestamp >= start && timestamp <= end) {
            if (value_index < values->size()) {
                results.emplace_back(timestamp, values->Get(value_index));
            }
            value_index++;
        } else if (timestamp > end) {
            break;  // 时间戳已超过查询范围，提前终止
        }
    }

    return results;
}

} // namespace tsdb
