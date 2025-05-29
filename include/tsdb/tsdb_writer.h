#pragma once

#include "../../include/tsdb/mmap_file.h"
#include "../../include/tsdb/delta_delta.h"
#include "../../include/tsdb/tsdb_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <memory>

namespace tsdb {

class TSDBWriter {
public:
    explicit TSDBWriter(const std::string& path, 
                       size_t initial_size = 1 << 30, // 1GB初始空间
                       const WriteOptions& options = {});

    // 写入单个数据点
    bool write(uint64_t timestamp, double value);

    // 批量写入
    bool write_batch(const std::vector<DataPoint>& points);

    // 显式刷盘
    void flush();

    ~TSDBWriter();

private:
    void write_to_mmap(const uint8_t* data, size_t size);
    void encode_point(uint64_t timestamp, double value);

    std::unique_ptr<MMapFile> mmap_file_;
    DeltaDeltaEncoder delta_encoder_;
    WriteOptions options_;
    std::vector<uint8_t> encode_buffer_;
    std::mutex mutex_;
    size_t buffered_count_ = 0;
};

} // namespace tsdb
