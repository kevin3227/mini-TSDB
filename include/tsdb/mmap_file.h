#pragma once
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>
#include <chrono>
#include <condition_variable>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tsdb {

class MMapFile {
public:
    // 构造函数：打开/创建文件并映射
    MMapFile(const std::string& path, size_t initial_size = 4096, bool read_only = false);
    
    // 禁用拷贝构造和赋值
    MMapFile(const MMapFile&) = delete;
    MMapFile& operator=(const MMapFile&) = delete;

    // 移动构造和赋值
    MMapFile(MMapFile&& other) noexcept;
    MMapFile& operator=(MMapFile&& other) noexcept;

    // 析构函数
    ~MMapFile();

    // 追加数据到 mmap 区域
    void append(const void* data, size_t size);

    // 获取当前 mmap 数据指针
    const uint8_t* data() const { return data_start_; }

    // 获取当前有效数据长度
    size_t length() const { return offset_; }

    // 获取整个 mmap 映射区域大小
    size_t size() const { return size_; }

    // 扩展 mmap 区域
    void expand(size_t needed_size);

    void enableAsyncFlush(bool enable, size_t batch_threshold = 1024 * 1024) {
        async_flush_ = enable;
        batch_threshold_ = batch_threshold;
        
        if (enable && !flush_thread_.joinable()) {
            running_ = true;
            flush_thread_ = std::thread(&MMapFile::flushThreadMain, this);
        }
    }

    void flushThreadMain() {
        while (running_) {
            size_t offset_to_flush = 0;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                flush_cv_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { 
                        return (offset_ - last_flushed_offset_ >= batch_threshold_) || 
                               !running_; 
                    });
                
                if (!running_) break;
                if (offset_ <= last_flushed_offset_) continue;
                
                offset_to_flush = offset_;
            }
            
            // 执行实际刷盘操作 (不持有锁)
            if (offset_to_flush > last_flushed_offset_) {
                // Linux特定API: 仅刷新mmap区域的部分数据
                msync(data_ + last_flushed_offset_, 
                      offset_to_flush - last_flushed_offset_,
                      MS_ASYNC);  // 异步刷盘
                
                std::lock_guard<std::mutex> lock(mtx_);
                last_flushed_offset_ = offset_to_flush;
                last_flush_time_ = std::chrono::steady_clock::now();
            }
        }
    }

private:
    std::string path_;
    int fd_ = -1;
    size_t size_ = 0;               // mmap 总大小（含 header）
    uint64_t* length_ptr_ = nullptr; // 指向头部中的 length 字段
    uint8_t* data_ = nullptr;        // mmap 原始地址
    uint8_t* data_start_ = nullptr;  // 实际数据起始位置（跳过 header）
    size_t offset_ = 0;              // 当前有效数据长度
    bool read_only_ = false;
    std::mutex mtx_;

    std::thread flush_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> async_flush_{false};
    std::condition_variable flush_cv_;
    size_t batch_threshold_ = 1024 * 1024; // 默认1MB批量阈值  
    size_t last_flushed_offset_ = 0;
    std::chrono::steady_clock::time_point last_flush_time_;
};

} // namespace tsdb