#include "tsdb/tsdb.h"
#include "tsdb/mmap_file.h"
#include "tsdb/delta_delta.h"

namespace tsdb {

TSDB::TSDB(const std::string& path) {
    file_ = new MMapFile(path);
    encoder_ = new DeltaDeltaEncoder();
}

TSDB::~TSDB() {
    delete file_;
    delete encoder_;
}

// void TSDB::append(uint64_t timestamp, double value) {
//     encoder_->addTimestamp(timestamp);
//     // TODO: Write to mmap file using FlatBuffers
// }

// std::vector<TSPoint> TSDB::queryRange(uint64_t start, uint64_t end) {
//     // TODO: Read from mmap and decode
//     return {};
// }
}