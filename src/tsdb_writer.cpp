#include "../include/tsdb/tsdb_writer.h"
#include "flatbuffers/flatbuffers.h"
#include "../include/tsdb/tsdb_generated.h"
#include "../include/tsdb/mmap_file.h"
#include <iostream>

namespace tsdb {

TSDBWriter::TSDBWriter(const std::string& path, 
                       size_t initial_size,
                       const WriteOptions& options)
    : mmap_file_(std::make_unique<MMapFile>(path, initial_size)),
      options_(options) {}

void TSDBWriter::encode_point(uint64_t timestamp, double value) {
    // Step 1: 压缩时间戳 (可选)
    if (options_.enable_delta_encoding) {
        delta_encoder_.addTimestamp(timestamp);
    }

    // Step 2: 构建FlatBuffer
    flatbuffers::FlatBufferBuilder builder(1024);
    auto point = CreateTimeSeriesPoint(builder, timestamp, value);
    builder.Finish(point);

    // Step 3: 写入缓冲区
    const uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    encode_buffer_.insert(encode_buffer_.end(), buf, buf + size);
    buffered_count_++;
}

void TSDBWriter::write_to_mmap(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 写入前检查剩余空间
    size_t remaining = mmap_file_->size() - mmap_file_->length();
    if (remaining < size + sizeof(uint32_t)) { // +4字节存储数据长度
        mmap_file_->expand(size + sizeof(uint32_t));
    }

    // 先写入数据长度(4字节)
    uint32_t len = static_cast<uint32_t>(size);
    mmap_file_->append(&len, sizeof(len));
    
    // 写入实际数据
    mmap_file_->append(data, size);
}

bool TSDBWriter::write(uint64_t timestamp, double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    encode_point(timestamp, value);
    
    // 达到batch_size时刷盘
    if (buffered_count_ >= options_.batch_size) {
        flush();
    }
    return true;
}

bool TSDBWriter::write_batch(const std::vector<DataPoint>& points) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [ts, val] : points) {
        encode_point(ts, val);
    }
    
    if (buffered_count_ >= options_.batch_size) {
        flush();
    }
    return true;
}

void TSDBWriter::flush() {
    if (encode_buffer_.empty()) return;
    
    write_to_mmap(encode_buffer_.data(), encode_buffer_.size());
    encode_buffer_.clear();
    buffered_count_ = 0;
}

TSDBWriter::~TSDBWriter() {
    flush(); // 确保析构时数据持久化
}

} // namespace tsdb
