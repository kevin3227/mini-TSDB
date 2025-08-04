// benchmark.cpp
#include <benchmark/benchmark.h>
#include "tsdb/tsdb_reader.h"
#include "tsdb/tsdb_writer.h"
#include <random>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

// 测试数据生成工具
class TestDataGenerator {
public:
    TestDataGenerator(uint64_t start_ts, uint64_t end_ts, double min_val, double max_val)
        : start_ts_(start_ts), ts_range_(end_ts - start_ts),
          val_dist_(min_val, max_val), 
          ts_dist_(0, ts_range_) {}
    
    std::pair<uint64_t, double> generate() {
        uint64_t ts = start_ts_ + ts_dist_(gen_);
        double val = val_dist_(gen_);
        return {ts, val};
    }

private:
    std::mt19937 gen_{std::random_device{}()};
    const uint64_t start_ts_;
    const uint64_t ts_range_;
    std::uniform_real_distribution<double> val_dist_;
    std::uniform_int_distribution<uint64_t> ts_dist_;
};

// 测试文件准备
static void prepare_test_file(const std::string& path, size_t num_points, bool compressed) {
    fs::remove(path); // 清理旧文件
    
    TestDataGenerator gen(1609459200000, 1609545600000, 0.0, 100.0); // 24小时范围
    tsdb::TSDBWriter writer(path, 1 << 24, compressed);
    
    for (size_t i = 0; i < num_points; ++i) {
        auto [ts, val] = gen.generate();
        writer.write(ts, val);
    }
}

// 基准测试基类
class TSDBFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& state) override {
        num_points_ = state.range(0);
        compressed_ = state.range(1);
        file_path_ = "/tmp/tsdb_bench.data";
        prepare_test_file(file_path_, num_points_, compressed_);
    }

    void TearDown(const benchmark::State&) override {
        fs::remove(file_path_);
    }

protected:
    size_t num_points_;
    bool compressed_;
    std::string file_path_;
};

// 写入性能测试
BENCHMARK_DEFINE_F(TSDBFixture, WritePerformance)(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        fs::remove(file_path_);
        tsdb::TSDBWriter writer(file_path_, 1 << 24, compressed_);
        TestDataGenerator gen(1609459200000, 1609545600000, 0.0, 100.0);
        state.ResumeTiming();
        
        for (size_t i = 0; i < num_points_; ++i) {
            auto [ts, val] = gen.generate();
            writer.write(ts, val);
        }
        writer.close();
    }
    
    state.SetItemsProcessed(state.iterations() * num_points_);
    state.SetBytesProcessed(state.iterations() * num_points_ * sizeof(double));
}

// 查询性能测试 - 固定范围
BENCHMARK_DEFINE_F(TSDBFixture, QueryFixedRange)(benchmark::State& state) {
    tsdb::TSDBReader reader(file_path_);
    const uint64_t start = 1609459200000 + 3600 * 1000; // 开始后1小时
    const uint64_t end = start + 600 * 1000; // 10分钟范围
    
    for (auto _ : state) {
        auto results = reader.query(start, end);
        benchmark::DoNotOptimize(results);
    }
    
    state.SetItemsProcessed(state.iterations());
}

// 查询性能测试 - 随机范围
BENCHMARK_DEFINE_F(TSDBFixture, QueryRandomRange)(benchmark::State& state) {
    tsdb::TSDBReader reader(file_path_);
    TestDataGenerator gen(1609459200000, 1609545600000, 0.0, 100.0);
    
    for (auto _state : state) {
        state.PauseTiming();
        auto [ts1, _] = gen.generate();
        auto [ts2, __] = gen.generate();
        uint64_t start = std::min(ts1, ts2);
        uint64_t end = std::max(ts1, ts2);
        state.ResumeTiming();
        
        auto results = reader.query(start, end);
        benchmark::DoNotOptimize(results);
    }
    
    state.SetItemsProcessed(state.iterations());
}

// 注册测试用例
BENCHMARK_REGISTER_F(TSDBFixture, WritePerformance)
    ->ArgsProduct({
        {1'000, 10'000, 100'000}, // 数据点数量
        // {false, true}             // 是否压缩
        {true}
    })
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_REGISTER_F(TSDBFixture, QueryFixedRange)
    ->ArgsProduct({
        {1'000, 10'000, 100'000, 1'000'000},
        // {false, true}
        {true}
    })
    ->Unit(benchmark::kMicrosecond)
    ->Threads(1)
    ->MeasureProcessCPUTime();

BENCHMARK_REGISTER_F(TSDBFixture, QueryRandomRange)
    ->ArgsProduct({
        {1'000, 10'000, 100'000, 1'000'000},
        // {false, true}
        {true}
    })
    ->Unit(benchmark::kMicrosecond)
    ->Threads(1)
    ->MeasureProcessCPUTime();

// 并发查询测试
static void BM_ConcurrentQueries(benchmark::State& state) {
    const std::string path = "/tmp/tsdb_concurrent.data";
    prepare_test_file(path, 1'000'000, true);
    tsdb::TSDBReader reader(path);
    
    std::vector<std::thread> threads;
    const int num_threads = state.range(0);
    std::atomic<int> queries_done{0};
    
    for (auto _ : state) {
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&] {
                TestDataGenerator gen(1609459200000, 1609545600000, 0.0, 100.0);
                auto [ts1, _] = gen.generate();
                auto [ts2, __] = gen.generate();
                auto results = reader.query(std::min(ts1, ts2), std::max(ts1, ts2));
                benchmark::DoNotOptimize(results);
                queries_done++;
            });
        }
        
        for (auto& t : threads) t.join();
        threads.clear();
    }
    
    state.SetItemsProcessed(queries_done);
    fs::remove(path);
}

BENCHMARK(BM_ConcurrentQueries)
    ->Arg(2)->Arg(4)->Arg(8)->Arg(16)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

BENCHMARK_MAIN();
