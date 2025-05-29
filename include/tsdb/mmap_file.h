#pragma once
#include <string>
#include <cstdint>
#include <mutex>

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
};

} // namespace tsdb