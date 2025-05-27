#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

namespace tsdb {

class DeltaDeltaEncoder {
public:
    DeltaDeltaEncoder();
    void addTimestamp(uint64_t timestamp);
    std::vector<uint8_t> finish();

private:
    uint64_t first_timestamp_ = 0;
    int64_t prev_delta_ = 0;
    std::vector<uint8_t> buffer_;
    
    void encodeZigZag(int64_t value, std::vector<uint8_t>& out);
};

class DeltaDeltaDecoder {
public:
    DeltaDeltaDecoder(const uint8_t* data, size_t size);
    bool next(uint64_t* out);

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;

    uint64_t first_timestamp_ = 0;
    int64_t prev_delta_ = 0;
    uint8_t state_ = 0; // 0: first timestamp, 1: deltas

    int64_t decodeZigZag(uint64_t value);
    bool readVarint(uint64_t* value);
};

} // namespace tsdb