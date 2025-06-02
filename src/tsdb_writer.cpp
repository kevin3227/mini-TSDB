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

    const uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();

    mmap_file_.append(buf, size);
}

void TSDBWriter::writeCompressed(uint64_t timestamp, double value) {
    delta_encoder_.addTimestamp(timestamp);
    values_.push_back(value);

    // 当前仅在 finish() 中统一写入压缩段
    // 这里可以选择按批处理，例如每 1000 个点写一次
    static const size_t BATCH_SIZE = 1000;
    if (values_.size() >= BATCH_SIZE) {
        flatbuffers::FlatBufferBuilder builder(1024);

        auto encoded = delta_encoder_.finish();
        // auto dd_vec = builder.CreateVector(encoded.data(), encoded.size());
        auto dd_vec = builder.CreateVector(reinterpret_cast<const int8_t*>(encoded.data()), encoded.size());
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        const uint8_t* buf = builder.GetBufferPointer();
        size_t size = builder.GetSize();

        mmap_file_.append(buf, size);

        // 清空缓存
        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();  // 重置编码器
    }
}

void TSDBWriter::close() {
    // 如果还有剩余未写入的数据，强制 flush
    if (compress_ && !values_.empty()) {
        flatbuffers::FlatBufferBuilder builder(1024);

        auto encoded = delta_encoder_.finish();
        // auto dd_vec = builder.CreateVector(encoded.data(), encoded.size());
        auto dd_vec = builder.CreateVector(reinterpret_cast<const int8_t*>(encoded.data()), encoded.size());
        auto values_vec = builder.CreateVector(values_);

        auto segment = CreateCompressedTimeSeriesSegment(builder, dd_vec, values_vec);
        builder.Finish(segment);

        const uint8_t* buf = builder.GetBufferPointer();
        size_t size = builder.GetSize();

        mmap_file_.append(buf, size);

        values_.clear();
        delta_encoder_ = DeltaDeltaEncoder();
    }

    mmap_file_.~MMapFile();  // 显式关闭
}

} // namespace tsdb