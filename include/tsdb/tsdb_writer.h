#pragma once

#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"

#include <vector>
#include <string>
#include <flatbuffers/flatbuffers.h>

namespace tsdb {

class TSDBWriter {
public:
    // 构造函数：打开或创建 mmap 文件
    explicit TSDBWriter(const std::string& path, size_t initial_size = 1 << 30, bool compress = true);

    // 写入一个时间序列点
    bool write(uint64_t timestamp, double value);

    // 关闭并持久化数据
    void close();

private:
    MMapFile mmap_file_;          // 封装 mmap 操作
    bool compress_;               // 是否启用压缩
    DeltaDeltaEncoder delta_encoder_;
    std::vector<double> values_;  // 值缓存（用于压缩模式）

    // 辅助函数：非压缩模式写入单个点
    void writeRaw(uint64_t timestamp, double value);

    // 辅助函数：压缩模式写入时间戳和值
    void writeCompressed(uint64_t timestamp, double value);
};

} // namespace tsdb