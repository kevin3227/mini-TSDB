#include "../include/tsdb/tsdb_writer.h"
#include <iostream>

int main() {
    tsdb::WriteOptions options;
    options.batch_size = 10000;
    
    tsdb::TSDBWriter writer("test.tsdb", 1 << 30, options);

    // 写入100万个数据点
    uint64_t base_time = 1700000000;
    for (int i = 0; i < 1'000'000; ++i) {
        writer.write(base_time + i * 10, i * 0.01); // 每10秒一个点
    }

    std::cout << "Write completed" << std::endl;
    return 0;
}
