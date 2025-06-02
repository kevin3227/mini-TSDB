#include "tsdb/tsdb_writer.h"
#include <iostream>

int main() {
    try {
        tsdb::TSDBWriter writer("test.tsdb", 1 << 20, true);  // 1MB 初始大小，启用压缩

        for (uint64_t i = 0; i < 100000; ++i) {
            uint64_t timestamp = 1700000000 + i * 1000;  // 每秒一个点
            double value = static_cast<double>(i) / 100.0;
            writer.write(timestamp, value);
        }

        writer.close();
        std::cout << "Wrote 100,000 time series points." << std::endl;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}