#include "tsdb/tsdb_writer.h"
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
        const int NUM_THREADS = 8;       // 增加线程数以测试并行性能
        const int POINTS_PER_THREAD = 10000; // 每个线程更多点
        const int TOTAL_POINTS = NUM_THREADS * POINTS_PER_THREAD;
        const int NUM_SHARDS = 4;        // 设置分片数量
        std::atomic<int> points_written(0); // 跟踪实际写入队列的点数
        
        // 定义全局唯一时间戳生成器
        std::atomic<uint64_t> global_timestamp(1700000000);
        
        // 删除旧的测试文件
        std::string base_path = "test.tsdb";
        if (std::filesystem::exists(base_path)) {
            std::filesystem::remove(base_path);
        }
        // 删除旧的分片文件
        for (int i = 0; i < NUM_SHARDS; i++) {
            std::string shard_path = base_path + ".shard" + std::to_string(i);
            if (std::filesystem::exists(shard_path)) {
                std::filesystem::remove(shard_path);
            }
        }

        // 创建写入器，指定分片数量
        tsdb::TSDBWriter writer(base_path, 1 << 20, true, 100, 50000, 100, NUM_SHARDS);
        
        // 跟踪所有生成的时间戳用于验证
        std::vector<uint64_t> all_timestamps;
        all_timestamps.reserve(TOTAL_POINTS);
        std::mutex ts_mutex;

        auto writer_task = [&](int thread_id) {
            std::mt19937 gen(thread_id);
            std::uniform_real_distribution<double> value_dist(0.0, 10.0);
            
            // 每个线程批量写入，提高效率
            std::vector<tsdb::TimePoint> batch;
            batch.reserve(100); // 积累100个点再批量写入
            
            for (int i = 0; i < POINTS_PER_THREAD; ++i) {
                // 生成全局唯一的时间戳
                uint64_t timestamp = global_timestamp.fetch_add(1);
                double value = value_dist(gen);
                
                batch.push_back({timestamp, value});
                
                // 记录时间戳用于验证
                {
                    std::lock_guard<std::mutex> lock(ts_mutex);
                    all_timestamps.push_back(timestamp);
                }
                
                // 当积累到一定数量或最后一个点时批量写入
                if (batch.size() >= 100 || i == POINTS_PER_THREAD - 1) {
                    if (!writer.write_batch(batch)) {
                        std::cerr << "Writer " << thread_id << " batch failed" << std::endl;
                    } else {
                        points_written += batch.size();
                    }
                    batch.clear();
                }
                
                // 添加少量延迟模拟真实场景
                if (i % 500 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(thread_id * 10));
                }
            }
        };

        std::cout << "Starting " << NUM_THREADS << " writers for " 
                 << TOTAL_POINTS << " points with " << NUM_SHARDS << " shards..." << std::endl;
                 
        auto start_time = std::chrono::steady_clock::now();
        
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(writer_task, i);
        }
        
        // 监控进度
        while (points_written < TOTAL_POINTS) {
            std::cout << "Progress: " << points_written << "/" 
                     << TOTAL_POINTS << " points (" 
                     << (points_written * 100.0 / TOTAL_POINTS) << "%)"
                     << "\r" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        writer.close();
        
        std::cout << "\nAll points written. Total time: " << duration << " ms" << std::endl;
        std::cout << "Write throughput: " << (TOTAL_POINTS * 1000.0 / duration) << " points/second" << std::endl;
        
        // 检查已创建的文件
        std::cout << "Checking created files..." << std::endl;
        for (int i = 0; i < NUM_SHARDS; i++) {
            std::string shard_path = base_path + ".shard" + std::to_string(i);
            if (std::filesystem::exists(shard_path)) {
                auto size = std::filesystem::file_size(shard_path);
                std::cout << "Shard " << i << ": " << size << " bytes" << std::endl;
            } else {
                std::cout << "Warning: Shard " << i << " not found!" << std::endl;
            }
        }
        
        std::cout << "Test completed successfully." << std::endl;
        
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    
    return 0;
}
