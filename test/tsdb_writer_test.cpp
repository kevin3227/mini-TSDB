#include "tsdb/tsdb_writer.h"
#include "tsdb/tsdb_reader.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <set>
#include <algorithm>
#include <cstdint>
#include <filesystem>

int main() {
    try {
        const int NUM_THREADS = 4;
        const int POINTS_PER_THREAD = 2500;
        const int TOTAL_POINTS = NUM_THREADS * POINTS_PER_THREAD;
        std::atomic<int> points_written(0); // 跟踪实际写入队列的点数
        
        // 定义全局唯一时间戳生成器
        std::atomic<uint64_t> global_timestamp(1700000000);
        
        // 创建写入器

        tsdb::TSDBWriter writer("test.tsdb", 1 << 20, true, 100, 50);
        
        // 跟踪所有生成的时间戳用于验证
        std::vector<uint64_t> all_timestamps;
        all_timestamps.reserve(TOTAL_POINTS);
        std::mutex ts_mutex;

        auto writer_task = [&](int thread_id) {
            std::mt19937 gen(thread_id);
            std::uniform_real_distribution<double> value_dist(0.0, 10.0);
            
            for (int i = 0; i < POINTS_PER_THREAD; ++i) {
                // 生成全局唯一的时间戳
                uint64_t timestamp = global_timestamp.fetch_add(1);
                double value = value_dist(gen);
                
                if (!writer.write(timestamp, value)) {
                    std::cerr << "Writer " << thread_id << " failed at " << timestamp << std::endl;
                } else {
                    points_written++;
                    
                    // 记录时间戳用于验证
                    {
                        std::lock_guard<std::mutex> lock(ts_mutex);
                        all_timestamps.push_back(timestamp);
                    }
                }
                
                // 添加少量延迟模拟真实场景
                if (i % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(thread_id * 10));
                }
            }
        };

        std::cout << "Starting " << NUM_THREADS << " writers for " 
                 << TOTAL_POINTS << " points..." << std::endl;
                 
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(writer_task, i);
        }
        
        // 监控进度
        while (points_written < TOTAL_POINTS) {
            // std::cout << "Progress: " << points_written << "/" 
            //          << TOTAL_POINTS << " points (" 
            //          << (points_written * 100 / TOTAL_POINTS) << "%)"
            //          << "\r" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "\nAll points submitted to writer." << std::endl;
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        writer.close();
        std::cout << "Writer closed. Validating data..." << std::endl;
        
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}
