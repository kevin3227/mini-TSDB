#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

// ZigZag encoding for signed integers
uint64_t zigzag_encode(int64_t n);
int64_t zigzag_decode(uint64_t n);

// Varint encoding
std::vector<uint8_t> varint_encode(uint64_t value);
bool varint_decode(const uint8_t* data, size_t len, uint64_t* out, size_t* bytes_read);
