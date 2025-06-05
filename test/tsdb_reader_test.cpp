#include "tsdb/tsdb_reader.h"
#include <iostream>

int main() {
    try {
        tsdb::TSDBReader reader("test.tsdb");
        
        // 查询时间范围 [1700000000, 1700005000]
        auto points = reader.query(1700000000, 2000000000);
        
        std::cout << "Found " << points.size() << " points" << std::endl;
        // for (const auto& p : points) {
        //     std::cout << p.first << " => " << p.second << std::endl;
        // }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
