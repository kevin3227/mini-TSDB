#include <gtest/gtest.h>
#include "tsdb/tsdb_writer.h"
#include <filesystem>
#include <random>
#include <chrono>
#include <fstream>

namespace tsdb {

// 生成临时文件路径
std::string getTempFilePath() {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return "/tmp/tsdb_wal_test_" + std::to_string(now);
}

// 清理测试文件
void cleanupFiles(const std::string& path) {
    try {
        std::filesystem::remove(path + ".wal");
        std::filesystem::remove(path + ".checkpoint");
        
        // 清理分片文件
        for (int i = 0; i < 16; i++) {
            std::string shard_path = path + ".shard" + std::to_string(i);
            if (std::filesystem::exists(shard_path)) {
                std::filesystem::remove(shard_path);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error cleaning up: " << e.what() << std::endl;
    }
}

// 基本WAL操作测试
TEST(WALTest, BasicOperations) {
    std::string path = getTempFilePath();
    
    // 创建WAL并写入数据
    {
        WALWriter wal(path);
        wal.logPoint(0, 100, 1.1);
        wal.logPoint(1, 200, 2.2);
        
        std::vector<TimePoint> batch1 = {
            {300, 3.3},
            {400, 4.4}
        };
        wal.logBatch(0, batch1);
        
        std::vector<TimePoint> batch2 = {
            {500, 5.5},
            {600, 6.6},
            {700, 7.7}
        };
        wal.logBatch(2, batch2);
        
        // 手动关闭确保数据写入
        wal.close();
    }
    
    // 重新打开WAL并验证恢复
    {
        WALWriter wal(path);
        // 测试恢复功能
        std::map<uint32_t, std::vector<std::pair<uint64_t, double>>> recovered_points;
        
        size_t count = wal.recover([&recovered_points](uint32_t shard, uint64_t ts, double val) {
            recovered_points[shard].push_back({ts, val});
        });
        
        // 验证恢复的点数
        EXPECT_EQ(count, 7);  // 1 + 1 + 2 + 3 = 7 个点
        
        // 确保结果中包含所有分片
        ASSERT_TRUE(recovered_points.find(0) != recovered_points.end());
        ASSERT_TRUE(recovered_points.find(1) != recovered_points.end());
        ASSERT_TRUE(recovered_points.find(2) != recovered_points.end());
        
        // 验证各分片恢复的点数
        EXPECT_EQ(recovered_points[0].size(), 3);  // 1个单点 + 2个批量点
        EXPECT_EQ(recovered_points[1].size(), 1);  // 1个单点
        EXPECT_EQ(recovered_points[2].size(), 3);  // 3个批量点
        
        // 验证具体值
        if (recovered_points[0].size() >= 1)
            EXPECT_DOUBLE_EQ(recovered_points[0][0].second, 1.1);
        if (recovered_points[1].size() >= 1)
            EXPECT_DOUBLE_EQ(recovered_points[1][0].second, 2.2);
        if (recovered_points[0].size() >= 3) {
            EXPECT_DOUBLE_EQ(recovered_points[0][1].second, 3.3);
            EXPECT_DOUBLE_EQ(recovered_points[0][2].second, 4.4);
        }
        if (recovered_points[2].size() >= 3) {
            EXPECT_DOUBLE_EQ(recovered_points[2][0].second, 5.5);
            EXPECT_DOUBLE_EQ(recovered_points[2][1].second, 6.6);
            EXPECT_DOUBLE_EQ(recovered_points[2][2].second, 7.7);
        }
        
        // 测试标记处理点后的恢复
        wal.markProcessed(0, std::numeric_limits<uint64_t>::max());  // 标记分片0已完全处理
        
        // 再次恢复，分片0的点不应再被处理
        std::map<uint32_t, std::vector<std::pair<uint64_t, double>>> recovered_again;
        count = wal.recover([&recovered_again](uint32_t shard, uint64_t ts, double val) {
            recovered_again[shard].push_back({ts, val});
        });
        
        // 只应恢复未处理的分片1和2的点
        EXPECT_EQ(count, 4);  // 1 + 3 = 4
        ASSERT_TRUE(recovered_again.find(0) == recovered_again.end()); // 分片0不应出现
    }
    
    cleanupFiles(path);
}

// 大数据批量测试
TEST(WALTest, LargeBatch) {
    std::string path = getTempFilePath();
    
    // 生成大批量数据
    std::vector<TimePoint> large_batch;
    const size_t BATCH_SIZE = 1000;
    large_batch.reserve(BATCH_SIZE);
    
    for (size_t i = 0; i < BATCH_SIZE; i++) {
        large_batch.push_back({1000 + i, static_cast<double>(i) * 0.1});
    }
    
    // 写入大批量数据
    {
        WALWriter wal(path);
        wal.logBatch(5, large_batch);
        wal.close();  // 确保数据写入
    }
    
    // 恢复并验证
    {
        WALWriter wal(path);
        std::vector<TimePoint> recovered;
        
        size_t count = wal.recover([&recovered](uint32_t, uint64_t ts, double val) {
            recovered.push_back({ts, val});
        });
        
        EXPECT_EQ(count, BATCH_SIZE);
        ASSERT_GE(recovered.size(), 2);
        
        // 验证第一个点和最后一个点
        if (recovered.size() >= 1) {
            EXPECT_EQ(recovered[0].timestamp, 1000);
            EXPECT_DOUBLE_EQ(recovered[0].value, 0.0);
        }
        
        if (recovered.size() >= BATCH_SIZE) {
            EXPECT_EQ(recovered[BATCH_SIZE-1].timestamp, 1000 + BATCH_SIZE - 1);
            EXPECT_DOUBLE_EQ(recovered[BATCH_SIZE-1].value, (BATCH_SIZE-1) * 0.1);
        }
    }
    
    cleanupFiles(path);
}

// 检查点测试
TEST(WALTest, Checkpoint) {
    std::string path = getTempFilePath();
    
    // 写入数据并设置检查点
    {
        WALWriter wal(path);
        
        wal.logPoint(0, 100, 1.1);
        wal.logPoint(1, 200, 2.2);
        wal.logPoint(2, 300, 3.3);
        
        // 标记分片0为已处理
        wal.markProcessed(0, std::numeric_limits<uint64_t>::max());
        
        // 关闭WAL，确保检查点写入
        wal.close();
    }
    
    // 重新打开WAL并验证检查点
    {
        WALWriter wal(path);
        
        // 应该只恢复分片1和2的点
        std::map<uint32_t, std::vector<std::pair<uint64_t, double>>> recovered;
        
        size_t count = wal.recover([&recovered](uint32_t shard, uint64_t ts, double val) {
            recovered[shard].push_back({ts, val});
        });
        
        EXPECT_EQ(count, 2);  // 只有分片1和2的点
        ASSERT_TRUE(recovered.find(0) == recovered.end());  // 分片0不应恢复
        ASSERT_TRUE(recovered.find(1) != recovered.end());
        ASSERT_TRUE(recovered.find(2) != recovered.end());
    }
    
    cleanupFiles(path);
}

// 空WAL测试
TEST(WALTest, EmptyWAL) {
    std::string path = getTempFilePath();
    
    // 创建空WAL
    {
        WALWriter wal(path);
        // 不写入任何数据
        wal.close();
    }
    
    // 从空WAL恢复
    {
        WALWriter wal(path);
        bool callback_called = false;
        
        size_t count = wal.recover([&callback_called](uint32_t, uint64_t, double) {
            callback_called = true;
        });
        
        EXPECT_EQ(count, 0);
        EXPECT_FALSE(callback_called);
    }
    
    cleanupFiles(path);
}

// 集成测试
TEST(WALIntegrationTest, BasicIntegration) {
    std::string path = "/tmp/tsdb_wal_integration_" + 
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    // 写入数据并验证
    {
        TSDBWriter writer(path, 1024*1024, true, 100, 1000, 50, 4);
        
        // 写入一些数据点
        for (int i = 0; i < 1000; i++) {
            ASSERT_TRUE(writer.write(1000 + i, i * 0.1));
        }
        
        // 批量写入
        std::vector<TimePoint> batch;
        for (int i = 0; i < 500; i++) {
            batch.push_back({static_cast<uint64_t>(2000 + i), i * 0.5});
        }
        ASSERT_TRUE(writer.write_batch(batch));
        
        // 正常关闭，确保所有数据刷新到磁盘
        writer.close();
    }
    
    // 验证WAL文件和元数据文件已创建
    ASSERT_TRUE(std::filesystem::exists(path + ".wal"));
    ASSERT_TRUE(std::filesystem::exists(path + ".checkpoint"));
    
    // 清理
    std::filesystem::remove(path + ".wal");
    std::filesystem::remove(path + ".checkpoint");
    for (int i = 0; i < 4; i++) {
        std::filesystem::remove(path + ".shard" + std::to_string(i));
    }
}

// 模拟崩溃恢复测试
TEST(WALIntegrationTest, RecoveryAfterCrash) {
    std::string path = "/tmp/tsdb_wal_recovery_" + 
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    const int TOTAL_POINTS = 1000;
    
    // 第一阶段：写入数据但不正常关闭（模拟崩溃）
    {
        TSDBWriter writer(path, 1024*1024, true, 100, 1000, 50, 4);
        
        // 写入一些数据点
        for (int i = 0; i < TOTAL_POINTS; i++) {
            writer.write(1000 + i, i * 0.1);
        }
        
        // 不调用close()，模拟崩溃
    }
    
    // 确保写入线程完全停止（模拟系统重启）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 第二阶段：重新打开，检查恢复
    {
        // 捕获标准输出以验证恢复消息
        testing::internal::CaptureStdout();
        
        TSDBWriter writer(path, 1024*1024, true, 100, 1000, 50, 4);
        
        std::string output = testing::internal::GetCapturedStdout();
        
        // 验证恢复消息包含恢复点数信息
        // 注意：实际恢复的点数可能因多线程处理而变化
        ASSERT_TRUE(output.find("Recovered") != std::string::npos);
        
        // 向恢复后的实例写入一些新数据
        for (int i = 0; i < 100; i++) {
            writer.write(2000 + i, i * 0.2);
        }
        
        // 正常关闭
        writer.close();
    }
    
    // 清理
    std::filesystem::remove(path + ".wal");
    std::filesystem::remove(path + ".checkpoint");
    for (int i = 0; i < 4; i++) {
        std::filesystem::remove(path + ".shard" + std::to_string(i));
    }
}

} // namespace tsdb
