#pragma once
#include <cstdint>
#include <utility>
#include <cstddef>

namespace tsdb {

using DataPoint = std::pair<uint64_t, double>; // <timestamp, value>

// 写入选项
struct WriteOptions {
    bool enable_delta_encoding = true; // 是否启用Delta压缩
    size_t batch_size = 1000;         // 批量写入大小
};

} // namespace tsdb
