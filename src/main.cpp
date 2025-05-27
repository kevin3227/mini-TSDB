#include "tsdb/tsdb.h"
#include <iostream>

int main() {
    std::cout << "hello tsdb" << std::endl;
    
    // TSDB db("data.mmap");
    // db.append(1000, 1.5);
    // db.append(1005, 2.5);
    // db.append(1012, 3.5);

    // auto results = db.queryRange(1000, 1010);
    // for (auto& p : results) {
    //     std::cout << p.timestamp << ": " << p.value << std::endl;
    // }

    return 0;
}