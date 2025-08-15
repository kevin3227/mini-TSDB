#include "tsdb/tsdb_reader.h"
#include <iostream>

int main() {
    try {
        tsdb::TSDBReader reader("test.tsdb");
        
        // 查询时间范围 [1700000000, 1700005000]
        auto points = reader.query(1700000000, 2000000000);
        
        std::cout << "Found " << points.size() << " points" << std::endl;
        // 打印前10个点
        for (size_t i = 0; i < std::min(points.size(), size_t(10)); ++i) {
            std::cout << "Timestamp: " << points[i].first 
                      << ", Value: " << points[i].second << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
