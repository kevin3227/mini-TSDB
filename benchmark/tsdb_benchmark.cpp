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

class PrecisionTimer {
public:
    void start() { start_time = std::chrono::high_resolution_clock::now(); }
    
    double stop() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double>(end_time - start_time).count();
    }
    
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
};

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
    
    std::pair<uint64_t, double> generateSequential(uint64_t offset) {
        uint64_t ts = start_ts_ + offset;
        double val = val_dist_(gen_);
        return {ts, val};
    }

    // 预生成批量数据减少运行时开销
    std::vector<std::pair<uint64_t, double>> generateBatch(size_t count) {
        std::vector<std::pair<uint64_t, double>> batch;
        batch.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            batch.push_back(generateSequential(i));
        }
        return batch;
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

// 清理所有分片文件
void cleanupShardFiles(const std::string& base_path, int max_shards = 32) {
    if (fs::exists(base_path)) {
        fs::remove(base_path);
    }
    
    std::string wal_path = base_path + ".wal";
    if (fs::exists(wal_path)) {
        fs::remove(wal_path);
    }
    
    std::string checkpoint_path = base_path + ".checkpoint";
    if (fs::exists(checkpoint_path)) {
        fs::remove(checkpoint_path);
    }
    
    for (int i = 0; i < max_shards; i++) {
        std::string shard_path = base_path + ".shard" + std::to_string(i);
        if (fs::exists(shard_path)) {
            fs::remove(shard_path);
        }
        
        std::string shard_wal = shard_path + ".wal";
        if (fs::exists(shard_wal)) {
            fs::remove(shard_wal);
        }
    }
}

// 多线程写入基准测试
static void BM_MultiThreadWrite(benchmark::State& state) {
    const size_t num_points = state.range(0);
    const size_t num_threads = state.range(1);
    const bool compressed = state.range(2) != 0;
    const size_t num_shards = state.range(3);
    const std::string file_path = "./tsdb_sharded_bench_" + 
                                 std::to_string(num_shards) + "_" +
                                 std::to_string(num_threads) + ".data";
    
    // 预生成所有测试数据（排除生成时间影响）
    std::vector<std::vector<std::pair<uint64_t, double>>> thread_data(num_threads);
    const size_t points_per_thread = num_points / num_threads;
    
    // 使用高精度定时器
    PrecisionTimer timer;
    double total_write_time = 0.0;
    size_t total_bytes_written = 0;
    
    for (size_t t = 0; t < num_threads; ++t) {
        TestDataGenerator gen(1609459200000, 1609545600000, 0.0, 100.0, t);
        thread_data[t] = gen.generateBatch(points_per_thread);
    }

    for (auto _ : state) {
        // 清理旧文件（不计时）
        cleanupShardFiles(file_path, num_shards);
        
        // 创建写入器（不计时）
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
        std::atomic<size_t> bytes_written{0};
        
        // 精确计时开始
        timer.start();
        
        // 启动写入线程
        for (size_t t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t, batch_data = std::ref(thread_data[t])]() {
                const size_t batch_size = 1000;
                auto& data = batch_data.get();
                
                for (size_t i = 0; i < data.size(); i += batch_size) {
                    auto start = data.begin() + i;
                    auto end = (i + batch_size < data.size()) 
                             ? start + batch_size : data.end();
                             
                    std::vector<tsdb::TimePoint> batch;
                    batch.reserve(batch_size);
                    std::transform(start, end, std::back_inserter(batch),
                        [](const auto& p) {
                            return tsdb::TimePoint{p.first, p.second};
                        });
                    
                    if (writer->write_batch(batch)) {
                        total_written += batch.size();
                        // 计算实际写入字节：每个点=8字节时间戳 + 8字节值
                        bytes_written += batch.size() * (sizeof(uint64_t) + sizeof(double));
                    }
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        // 刷新并关闭写入器（计入写入时间）
        writer->flush();
        writer->close();
        
        // 精确计时结束
        double elapsed = timer.stop();
        total_write_time += elapsed;
        
        // 验证写入完整性
        if (total_written != num_points) {
            state.SkipWithError("Point count mismatch");
            cleanupShardFiles(file_path, num_shards);
            return;
        }
        
        total_bytes_written += bytes_written.load();
    }
    
    // 计算并报告精确吞吐量指标
    const double avg_time = total_write_time / state.iterations();
    const double points_per_sec = num_points / avg_time;
    const double mb_per_sec = (total_bytes_written / state.iterations()) / (1024 * 1024) / avg_time;
    
    state.counters["Points/s"] = benchmark::Counter(points_per_sec, benchmark::Counter::kIsRate);
    // state.counters["MB/s"] = benchmark::Counter(mb_per_sec, benchmark::Counter::kIsRate);
    // state.counters["Latency"] = benchmark::Counter(avg_time * 1000, benchmark::Counter::kAvgThreads);
    
    // state.SetItemsProcessed(state.iterations() * num_points);
    // state.SetBytesProcessed(total_bytes_written);
    
    // 最终清理
    cleanupShardFiles(file_path, num_shards);
}

// 注册基准测试
BENCHMARK(BM_MultiThreadWrite)
    ->ArgsProduct({
        {1000000},        // 点数
        {2, 4, 8, 16},    // 线程数
        {1},              // 压缩启用
        {1, 2, 4, 8}      // 分片数
    })
    ->MeasureProcessCPUTime()
    ->UseRealTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
