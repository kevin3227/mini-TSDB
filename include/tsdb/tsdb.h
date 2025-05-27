#pragma once

#include <string>
#include <vector>

namespace tsdb {

class MMapFile;
class DeltaDeltaEncoder;

struct TSPoint {
    uint64_t timestamp;
    double value;
};

class TSDB {
public:
    TSDB(const std::string& path);
    ~TSDB();

    void append(uint64_t timestamp, double value);
    std::vector<TSPoint> queryRange(uint64_t start, uint64_t end);

private:
    MMapFile* file_;
    DeltaDeltaEncoder* encoder_;
};
}