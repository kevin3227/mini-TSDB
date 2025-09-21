#include "tsdb/tsdb_writer.h"
#include <thread>
#include <chrono>
#include <iostream>

int main() {
    try {
        // 销毁旧的测试数据目录
        std::filesystem::remove_all("/tmp/test_tsdb");
        
        // 创建带监控的TSDB写入器
        tsdb::TSDBWriter writer("/tmp/test_tsdb", 1<<20, true, 1000, 10000, 100, 4, true);
        
        // std::cout << "开始写入数据，监控将在1秒后开始显示..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 模拟数据写入
        uint64_t timestamp = 1000000;
        for (int batch = 0; batch < 100; ++batch) {
            std::vector<tsdb::TimePoint> points;
            for (int i = 0; i < 1000; ++i) {
                points.push_back({timestamp++, static_cast<double>(rand()) / RAND_MAX});
            }
            
            writer.write_batch(points);
            
            // 每批次间稍微延迟，以便观察监控效果
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // std::cout << "数据写入完成，等待10秒观察监控..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
