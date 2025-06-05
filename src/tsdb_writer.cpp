#include "tsdb/tsdb_writer.h"
#include "tsdb/tsdb_generated.h"  // 自动生成的 FlatBuffers 头文件
#include <flatbuffers/flatbuffers.h>
#include <stdexcept>
#include <iostream>

namespace tsdb {

TSDBWriter::TSDBWriter(const std::string& path, size_t initial_size, bool compress)
    : mmap_file_(path, initial_size, /*read_only=*/false), compress_(compress) {}

bool TSDBWriter::write(uint64_t timestamp, double value) {
    if (compress_) {
        writeCompressed(timestamp, value);
    } else {
        writeRaw(timestamp, value);
    }
    return true;
}

void TSDBWriter::writeRaw(uint64_t timestamp, double value) {
    flatbuffers::FlatBufferBuilder builder(1024);

    auto point = CreateTimeSeriesPoint(builder, timestamp, value);
    builder.Finish(point);

    // 写入大小前缀
    uint32_t size = builder.GetSize();
    mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
    
    // 写入数据
    mmap_file_.append(builder.GetBufferPointer(), size);
}

void TSDBWriter::writeCompressed(uint64_t timestamp, double value) {
    delta_encoder_.addTimestamp(timestamp);
    values_.push_back(value);

    static const size_t BATCH_SIZE = 1000;
    if (values_.size() >= BATCH_SIZE) {
        flatbuffers::FlatBufferBuilder builder(1024);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(reinterpret_cast<const int8_t*>(encoded.data()), encoded.size());
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀
        uint32_t size = builder.GetSize();
        mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
        
        // 写入数据
        mmap_file_.append(builder.GetBufferPointer(), size);

        // 清空缓存
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
    }
}

void TSDBWriter::close() {
    if (compress_ && !values_.empty()) {
        flatbuffers::FlatBufferBuilder builder(1024);

        auto encoded = delta_encoder_.finish();
        auto dd_vec = builder.CreateVector(reinterpret_cast<const int8_t*>(encoded.data()), encoded.size());
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        // 写入大小前缀
        uint32_t size = builder.GetSize();
        mmap_file_.append(reinterpret_cast<const uint8_t*>(&size), sizeof(size));
        
        // 写入数据
        mmap_file_.append(builder.GetBufferPointer(), size);

        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
    }

    mmap_file_.~MMapFile();
}

} // namespace tsdb
