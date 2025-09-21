#include "tsdb/wal.h"
#include "tsdb/tsdb_pch.h"

namespace tsdb {

WALWriter::WALWriter(const std::string& path, size_t initial_size)
    : wal_file_(path + ".wal", initial_size), path_(path) {
    
    shard_positions_.resize(16, 0);
    
    // 读取最后的checkpoint
    readCheckpoint();
}

WALWriter::~WALWriter() {
    close();
}

void WALWriter::logPoint(uint32_t shard_id, uint64_t timestamp, double value) {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    // 1. 准备头部
    uint8_t header[9]; // 1字节操作类型 + 4字节分片ID + 4字节数据长度
    header[0] = OP_POINT;
    *reinterpret_cast<uint32_t*>(header + 1) = shard_id;
    *reinterpret_cast<uint32_t*>(header + 5) = sizeof(timestamp) + sizeof(value);
    
    // 2. 写入头部
    wal_file_.append(header, sizeof(header));
    
    // 3. 写入数据点
    wal_file_.append(&timestamp, sizeof(timestamp));
    wal_file_.append(&value, sizeof(value));
    
    total_entries_++;
}

void WALWriter::logBatch(uint32_t shard_id, const std::vector<TimePoint>& points) {
    if (points.empty()) return;
    
    std::lock_guard<std::mutex> lock(wal_mutex_);
    
    // 1. 准备头部
    uint8_t header[13]; // 1字节操作类型 + 4字节分片ID + 4字节点数量 + 4字节数据长度
    header[0] = OP_BATCH;
    *reinterpret_cast<uint32_t*>(header + 1) = shard_id;
    uint32_t point_count = points.size();
    *reinterpret_cast<uint32_t*>(header + 5) = point_count;
    *reinterpret_cast<uint32_t*>(header + 9) = point_count * (sizeof(uint64_t) + sizeof(double));
    
    // 2. 写入头部
    wal_file_.append(header, sizeof(header));
    
    // 3. 写入批量数据点
    for (const auto& point : points) {
        wal_file_.append(&point.timestamp, sizeof(point.timestamp));
        wal_file_.append(&point.value, sizeof(point.value));
    }
    
    total_entries_ += points.size();
}

void WALWriter::markProcessed(uint32_t shard_id, uint64_t offset) {
    if (shard_id >= shard_positions_.size()) {
        shard_positions_.resize(shard_id + 1, 0);
    }
    
    shard_positions_[shard_id] = offset;
    
    // 定期写入checkpoint
    writeCheckpoint();
}

uint64_t WALWriter::getRecoveryPoint(uint32_t shard_id) {
    if (shard_id >= shard_positions_.size()) {
        return 0;
    }
    return shard_positions_[shard_id];
}

void WALWriter::writeCheckpoint() {
    std::string meta_path = path_ + ".checkpoint";
    
    try {
        // 使用较小的初始大小，因为checkpoint文件通常很小
        MMapFile checkpoint_file(meta_path, 4096, false);
        
        // 写入分片数量
        uint32_t shard_count = shard_positions_.size();
        checkpoint_file.append(&shard_count, sizeof(shard_count));
        
        // 写入所有分片位置数据
        if (shard_count > 0) {
            checkpoint_file.append(shard_positions_.data(), 
                                  shard_count * sizeof(uint64_t));
        }
        
        // MMapFile析构时会自动处理映射和同步
    } catch (const std::exception& e) {
        std::cerr << "Failed to write WAL checkpoint: " << e.what() << std::endl;
    }
}

void WALWriter::readCheckpoint() {
    std::string meta_path = path_ + ".checkpoint";
    
    try {
        // 尝试以只读方式打开检查点文件
        MMapFile checkpoint_file(meta_path, 0, true);
        
        // 检查文件是否有足够数据
        if (checkpoint_file.length() < sizeof(uint32_t)) {
            return;  // 文件太小，没有有效数据
        }
        
        // 读取分片数量
        const uint8_t* data = checkpoint_file.data();
        uint32_t shard_count = *reinterpret_cast<const uint32_t*>(data);
        
        // 验证数据完整性
        size_t expected_size = sizeof(uint32_t) + shard_count * sizeof(uint64_t);
        if (checkpoint_file.length() < expected_size) {
            std::cerr << "Incomplete checkpoint file, expected " << expected_size 
                      << " bytes but got " << checkpoint_file.length() << std::endl;
            return;
        }
        
        // 读取分片位置数据
        if (shard_count > 0) {
            shard_positions_.resize(shard_count);
            memcpy(shard_positions_.data(), 
                  data + sizeof(uint32_t), 
                  shard_count * sizeof(uint64_t));
        }
        
    } catch (const std::exception& e) {
        // 文件可能不存在或损坏，这是正常的首次启动情况
        // std::cout << "No valid WAL checkpoint found, starting fresh" << std::endl;
    }
}

size_t WALWriter::recover(std::function<void(uint32_t, uint64_t, double)> point_handler) {
    if (!point_handler) return 0;
    
    std::lock_guard<std::mutex> lock(wal_mutex_);
    size_t recovered_points = 0;
    
    // 获取WAL文件数据
    const uint8_t* data = wal_file_.data();
    size_t length = wal_file_.length();
    
    if (length == 0) return 0;  // 空文件直接返回
    
    const uint8_t* end = data + length;
    const uint8_t* pos = data;
    
    // 调试信息
    // std::cout << "Starting WAL recovery, data length: " << length << std::endl;
    
    // 遍历WAL记录
    while (pos + 1 < end) {
        // 保存当前记录起始位置
        const uint8_t* record_start = pos;
        
        WALOpType op_type = static_cast<WALOpType>(*pos);
        pos++;
        
        // 确保有足够的字节读取分片ID
        if (pos + 4 > end) break;
        
        uint32_t shard_id = *reinterpret_cast<const uint32_t*>(pos);
        pos += 4;
        
        // 获取该分片的恢复点
        uint64_t recovery_offset = getRecoveryPoint(shard_id);
        
        // 当前WAL记录的全局偏移
        uint64_t current_offset = record_start - data;
        
        // 检查是否需要跳过此记录（已处理）
        bool skip_record = (recovery_offset != 0 && current_offset <= recovery_offset);
        
        // 调试 
        // std::cout << "Record at offset " << current_offset << ", type: " << (int)op_type 
        //           << ", shard: " << shard_id << ", recovery point: " << recovery_offset 
        //           << ", skip: " << skip_record << std::endl;
        
        // 处理单点记录
        if (op_type == OP_POINT) {
            // 确保有足够字节读取数据长度
            if (pos + 4 > end) break;
            
            uint32_t data_len = *reinterpret_cast<const uint32_t*>(pos);
            pos += 4;
            
            // 检查数据是否完整
            if (pos + data_len <= end) {
                if (!skip_record) {
                    // 期望数据长度为时间戳+值
                    if (data_len >= sizeof(uint64_t) + sizeof(double)) {
                        uint64_t timestamp = *reinterpret_cast<const uint64_t*>(pos);
                        double value = *reinterpret_cast<const double*>(pos + sizeof(uint64_t));
                        
                        // 调用回调处理该点
                        point_handler(shard_id, timestamp, value);
                        recovered_points++;
                        
                        // 调试 
                        // std::cout << "Recovered point: shard=" << shard_id 
                        //           << ", ts=" << timestamp << ", val=" << value << std::endl;
                    }
                }
                // 移动到下一条记录
                pos += data_len;
            } else {
                // 数据不完整，中断处理
                std::cerr << "Incomplete data for OP_POINT record" << std::endl;
                break;
            }
        }
        // 处理批量记录
        else if (op_type == OP_BATCH) {
            // 确保有足够字节读取点数和数据长度
            if (pos + 8 > end) break;
            
            uint32_t point_count = *reinterpret_cast<const uint32_t*>(pos);
            pos += 4;
            uint32_t data_len = *reinterpret_cast<const uint32_t*>(pos);
            pos += 4;
            
            // 检查数据是否完整
            if (pos + data_len <= end) {
                if (!skip_record) {
                    // 逐个处理批次中的点
                    const uint8_t* batch_pos = pos;
                    const uint8_t* batch_end = pos + data_len;
                    
                    // std::cout << "Processing batch with " << point_count << " points, data_len: " 
                    //           << data_len << std::endl;
                    
                    for (uint32_t i = 0; i < point_count && batch_pos + sizeof(uint64_t) + sizeof(double) <= batch_end; i++) {
                        uint64_t timestamp = *reinterpret_cast<const uint64_t*>(batch_pos);
                        batch_pos += sizeof(uint64_t);
                        double value = *reinterpret_cast<const double*>(batch_pos);
                        batch_pos += sizeof(double);
                        
                        // 调用回调处理该点
                        point_handler(shard_id, timestamp, value);
                        recovered_points++;
                        
                        if (i < 3 || i >= point_count - 3) {  // 只显示前3个和后3个点
                            // std::cout << "Batch point " << i << ": ts=" << timestamp 
                            //           << ", val=" << value << std::endl;
                        }
                    }
                }
                // 移动到下一条记录
                pos += data_len;
            } else {
                // 数据不完整，中断处理
                std::cerr << "Incomplete data for OP_BATCH record" << std::endl;
                break;
            }
        }
        // 未知记录类型
        else {
            std::cerr << "Unknown WAL record type: " << (int)op_type << " at offset " << current_offset << std::endl;
            break;
        }
    }
    
    // std::cout << "WAL recovery completed, recovered " << recovered_points << " points" << std::endl;
    return recovered_points;
}

uint64_t WALWriter::getCurrentPosition() {
    std::lock_guard<std::mutex> lock(wal_mutex_);
    return wal_file_.length();
}

void WALWriter::close() {
    writeCheckpoint();
}

} // namespace tsdb
