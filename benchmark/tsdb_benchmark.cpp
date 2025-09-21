#include <benchmark/benchmark.h>
#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_writer.h"
#include <random>
#include <filesystem>
#include <thread>
#include <atomic>
#include <memory>
#include <chrono>
#include <functional>

namespace fs = std::filesystem;

// 测试数据生成工具
class TestDataGenerator {
public:
    TestDataGenerator(uint64_t start_ts, uint64_t end_ts, double min_val, double max_val, uint64_t seed = 0)
        : start_ts_(start_ts), ts_range_(end_ts - start_ts),
          val_dist_(min_val, max_val), 
          ts_dist_(0, ts_range_),
          gen_(seed ? seed : std::random_device{}()) {}
    
    std::pair<uint64_t, double> generate() {
        uint64_t ts = start_ts_ + ts_dist_(gen_);
        double val = val_dist_(gen_);
        return {ts, val};
    }
    
    // 生成连续的时间戳（用于模拟顺序写入）
    std::pair<uint64_t, double> generateSequential(uint64_t offset) {
        uint64_t ts = start_ts_ + offset;
        double val = val_dist_(gen_);
        return {ts, val};
    }

private:
    std::mt19937 gen_;
    const uint64_t start_ts_;
    const uint64_t ts_range_;
    std::uniform_real_distribution<double> val_dist_;
    std::uniform_int_distribution<uint64_t> ts_dist_;
};

// 合并多个分片的查询结果
std::vector<std::pair<uint64_t, double>> mergeResults(
    const std::vector<std::vector<std::pair<uint64_t, double>>>& shard_results) {
    
    size_t total_size = 0;
    for (const auto& shard : shard_results) {
        total_size += shard.size();
    }
    
    std::vector<std::pair<uint64_t, double>> merged;
    merged.reserve(total_size);
    
    for (const auto& shard : shard_results) {
        merged.insert(merged.end(), shard.begin(), shard.end());
    }
    
    std::sort(merged.begin(), merged.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    
    return merged;
}

// 查找所有分片文件并执行查询
std::vector<std::pair<uint64_t, double>> queryAllShards(
    const std::string& base_path, uint64_t start_ts, uint64_t end_ts) {
    
    std::vector<std::string> shard_paths;
    shard_paths.push_back(base_path); // 添加主文件（如果存在）
    
    // 查找所有分片文件
    for (int i = 0; ; i++) {
        std::string shard_path = base_path + ".shard" + std::to_string(i);
        if (fs::exists(shard_path)) {
            shard_paths.push_back(shard_path);
        } else if (i > 0 || !fs::exists(base_path)) {
            // 找不到更多分片，或者主文件不存在
            break;
        }
    }
    

    // 为空的话退出
    if (shard_paths.empty() || 
        (shard_paths.size() == 1 && !fs::exists(shard_paths[0]))) {
        return {};
    }
    
    // 从每个分片读取数据
    std::vector<std::vector<std::pair<uint64_t, double>>> shard_results;
    for (const auto& path : shard_paths) {
        if (fs::exists(path)) {
            try {
                tsdb::TSDBReader reader(path);
                auto points = reader.query(start_ts, end_ts);
                if (!points.empty()) {
                    shard_results.push_back(std::move(points));
                }
            } catch (const std::exception&) {
                // 忽略读取错误，继续处理其他分片
            }
        }
    }
    
    // 合并结果
    return mergeResults(shard_results);
}

// 清理所有分片文件
void cleanupShardFiles(const std::string& base_path, int max_shards = 32) {
    if (fs::exists(base_path)) {
        fs::remove(base_path);
    }
    
    for (int i = 0; i < max_shards; i++) {
        std::string shard_path = base_path + ".shard" + std::to_string(i);
        if (fs::exists(shard_path)) {
            fs::remove(shard_path);
        }
    }
}

// 多线程写入基准测试
static void BM_MultiThreadWrite(benchmark::State& state) {
    const size_t num_points = state.range(0);
    const size_t num_threads = state.range(1);
    const bool compressed = state.range(2) != 0;
    const size_t num_shards = state.range(3);
    const std::string file_path = "/tmp/tsdb_sharded_bench_" + 
                                 std::to_string(num_shards) + ".data";
    
    // 点数必须能被线程数整除
    const size_t points_per_thread = num_points / num_threads;
    if (num_points % num_threads != 0) {
        state.SkipWithError("Points count must be divisible by thread count");
        return;
    }
    
    for (auto _ : state) {
        state.PauseTiming();
        // 清理旧文件
        cleanupShardFiles(file_path, num_shards);
        
        // 创建写入器（单文件或分片模式）
        std::unique_ptr<tsdb::TSDBWriter> writer;
        if (num_shards > 1) {
            writer = std::make_unique<tsdb::TSDBWriter>(
                file_path, 1 << 24, compressed, 1000, 50000, 100, num_shards, false);
        } else {
            writer = std::make_unique<tsdb::TSDBWriter>(
                file_path, 1 << 24, compressed);
        }
        
        std::vector<std::thread> threads;
        std::atomic<size_t> total_written{0};
        state.ResumeTiming();
        
        // 启动写入线程
        for (size_t t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, thread_id = t]() {
                // 使用线程ID作为随机种子，确保每个线程生成不同的数据
                TestDataGenerator gen(
                    1609459200000, 1609545600000, 0.0, 100.0, thread_id);
                
                // 每批写入的点数
                const size_t batch_size = 100;
                std::vector<tsdb::TimePoint> batch;
                batch.reserve(batch_size);
                
                for (size_t i = 0; i < points_per_thread; ++i) {
                    auto [ts, val] = gen.generateSequential(i * num_threads + thread_id);
                    batch.push_back({ts, val});
                    
                    if (batch.size() >= batch_size || i == points_per_thread - 1) {
                        if (writer->write_batch(batch)) {
                            total_written += batch.size();
                        }
                        batch.clear();
                    }
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        writer->close();
        
        // 验证写入点数
        if (total_written != num_points) {
            state.SkipWithError("Failed to write all points");
            return;
        }
    }
    
    // 统计指标
    state.SetItemsProcessed(state.iterations() * num_points);
    state.SetBytesProcessed(state.iterations() * num_points * (sizeof(double) + sizeof(uint64_t)));
    
    // 清理文件
    cleanupShardFiles(file_path, num_shards);
}

// 注册基准测试
BENCHMARK(BM_MultiThreadWrite)
    ->Args({1000000, 2, 1, 2})
    ->Args({1000000, 2, 1, 4})
    ->Args({1000000, 4, 1, 2})
    ->Args({1000000, 4, 1, 4})
    ->Args({1000000, 8, 1, 2})
    ->Args({1000000, 8, 1, 4})
    ->Args({1000000, 16, 1, 2})
    ->Args({1000000, 16, 1, 4})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();