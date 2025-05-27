// #include "tsdb/delta_delta.h"
// #include <iostream>
// #include <vector>

// int main() {
//     std::vector<uint64_t> timestamps = {1000, 1001, 1002, 1003, 1005, 1008, 1013};
//     tsdb::DeltaDeltaEncoder encoder;

//     for (auto ts : timestamps) {
//         encoder.addTimestamp(ts);
//     }

//     std::vector<uint8_t> encoded = encoder.finish();

//     tsdb::DeltaDeltaDecoder decoder(encoded.data(), encoded.size());
//     uint64_t ts;
//     int i = 0;
//     while (decoder.next(&ts)) {
//         std::cout << "Decoded: " << ts << " (Expected: " << timestamps[i++] << ")" << std::endl;
//         if (ts != timestamps[i - 1]) {
//             std::cerr << "Mismatch at index " << i - 1 << std::endl;
//             return 1;
//         }
//     }

//     std::cout << "All timestamps decoded successfully." << std::endl;
//     return 0;
// }