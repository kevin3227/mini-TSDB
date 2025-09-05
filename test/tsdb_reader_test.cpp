#include "tsdb/tsdb_reader.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <string>

// 合并多个分片的结果
std::vector<std::pair<uint64_t, double>> mergeResults(
    const std::vector<std::vector<std::pair<uint64_t, double>>>& shard_results) {
    
    // 确定总点数
    size_t total_size = 0;
    for (const auto& shard : shard_results) {
        total_size += shard.size();
    }
    
    // 合并所有点到一个向量
    std::vector<std::pair<uint64_t, double>> merged;
    merged.reserve(total_size);
    
    for (const auto& shard : shard_results) {
        merged.insert(merged.end(), shard.begin(), shard.end());
    }
    
    // 按时间戳排序
    std::sort(merged.begin(), merged.end(), 
        [](const auto& a, const auto& b) { return a.first < b.first; });
    
    return merged;
}

int main() {
    try {
        // 基本文件路径
        std::string base_path = "test.tsdb";
        
        // 查找所有分片文件
        std::vector<std::string> shard_paths;
        shard_paths.push_back(base_path); // 添加主文件（如果存在）
        
        // 检查是否有分片文件
        for (int i = 0; ; i++) {
            std::string shard_path = base_path + ".shard" + std::to_string(i);
            if (std::filesystem::exists(shard_path)) {
                shard_paths.push_back(shard_path);
            } else if (i > 0) {
                // 如果找不到序号为i的分片，并且i>0，说明已经找完所有分片
                break;
            } else if (i == 0 && shard_paths.size() == 1) {
                // 如果找不到shard0，并且只有主文件，检查主文件是否存在
                if (!std::filesystem::exists(base_path)) {
                    std::cerr << "No TSDB files found!" << std::endl;
                    return 1;
                }
                // 主文件存在但没有分片，说明是旧版单文件模式
                break;
            } else if (i == 0) {
                // 如果找不到shard0，但有其他分片，继续查找
                continue;
            }
        }
        
        std::cout << "Found " << shard_paths.size() << " TSDB files" << std::endl;
        
        // 查询时间范围 [1700000000, 2000000000]
        std::vector<std::vector<std::pair<uint64_t, double>>> shard_results;
        uint64_t start_ts = 1700000000;
        uint64_t end_ts = 2000000000;
        
        // 从每个分片读取数据
        for (const auto& path : shard_paths) {
            try {
                tsdb::TSDBReader reader(path);
                auto points = reader.query(start_ts, end_ts);
                if (!points.empty()) {
                    shard_results.push_back(std::move(points));
                    std::cout << "Read " << shard_results.back().size() 
                              << " points from " << path << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not read from " << path 
                          << ": " << e.what() << std::endl;
                // 继续处理其他分片
            }
        }
        
        // 合并结果
        auto merged_points = mergeResults(shard_results);
        
        std::cout << "Total found " << merged_points.size() << " points" << std::endl;
        
        // 打印前10个点
        for (size_t i = 0; i < std::min(merged_points.size(), size_t(10)); ++i) {
            std::cout << "Timestamp: " << merged_points[i].first 
                      << ", Value: " << merged_points[i].second << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
