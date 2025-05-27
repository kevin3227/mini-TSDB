#include "tsdb/delta_delta.h"
#include <cassert>
#include <cstddef>

namespace tsdb {

// DeltaDeltaEncoder 构造函数
DeltaDeltaEncoder::DeltaDeltaEncoder() {}

// ZigZag 编码：将有符号整数映射到无符号整数以便 Varint 编码
void DeltaDeltaEncoder::encodeZigZag(int64_t value, std::vector<uint8_t>& out) {
    uint64_t zigzag = (static_cast<uint64_t>(value) << 1) ^ (static_cast<uint64_t>(value) >> 63);
    while (zigzag > 0x7F) {
        out.push_back(static_cast<uint8_t>((zigzag & 0x7F) | 0x80));
        zigzag >>= 7;
    }
    out.push_back(static_cast<uint8_t>(zigzag));
}

// 添加时间戳进行编码
void DeltaDeltaEncoder::addTimestamp(uint64_t timestamp) {
    if (buffer_.empty()) {
        first_timestamp_ = timestamp;
        return;
    }

    int64_t delta = static_cast<int64_t>(timestamp - first_timestamp_);
    int64_t deltadelta = delta - prev_delta_;

    encodeZigZag(deltadelta, buffer_);

    prev_delta_ = delta;
}

// 返回编码后的字节流
std::vector<uint8_t> DeltaDeltaEncoder::finish() {
    std::vector<uint8_t> result;

    // 存储第一个时间戳（作为普通 varint）
    uint64_t val = first_timestamp_;
    while (val > 0x7F) {
        result.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    result.push_back(static_cast<uint8_t>(val));

    // 合并后续的 deltadelta 编码
    result.insert(result.end(), buffer_.begin(), buffer_.end());
    return result;
}

// 解码构造函数
DeltaDeltaDecoder::DeltaDeltaDecoder(const uint8_t* data, size_t size)
    : data_(data), size_(size) {}

// Varint 解码
bool DeltaDeltaDecoder::readVarint(uint64_t* value) {
    *value = 0;
    uint64_t shift = 0;
    while (pos_ < size_) {
        uint8_t byte = data_[pos_++];
        *value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
        if (shift > 64) {
            return false; // too big
        }
    }
    return false; // incomplete
}

// ZigZag 解码
int64_t DeltaDeltaDecoder::decodeZigZag(uint64_t value) {
    return static_cast<int64_t>((value >> 1) ^ -(value & 1));
}

// 读取下一个时间戳
bool DeltaDeltaDecoder::next(uint64_t* out) {
    if (state_ == 0) {
        // 第一个时间戳
        uint64_t val;
        if (!readVarint(&val)) return false;
        first_timestamp_ = val;
        prev_delta_ = 0;
        *out = first_timestamp_;
        state_ = 1;
        return true;
    } else {
        // 后续的 Delta-of-Delta
        uint64_t val;
        if (!readVarint(&val)) return false;
        int64_t dd = decodeZigZag(val);
        prev_delta_ += dd;
        *out = first_timestamp_ + prev_delta_;
        return true;
    }
}

} // namespace tsdb