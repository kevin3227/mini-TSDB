#include "tsdb/mmap_file.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>
#include <cstring>
#include <iostream>

namespace tsdb {

// 预留 8 字节作为元信息头，用于存储 length
constexpr size_t HEADER_SIZE = sizeof(uint64_t);

MMapFile::MMapFile(const std::string& path, size_t initial_size, bool read_only)
    : path_(path), read_only_(read_only) {

    // 计算总大小：header + 用户指定的初始大小
    size_t total_size = initial_size + HEADER_SIZE;

    // 打开文件
    int open_flags = read_only ? O_RDONLY : O_RDWR | O_CREAT;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH; // 0644

    fd_ = ::open(path.c_str(), open_flags, mode);
    if (fd_ == -1) {
        throw std::system_error(errno, std::system_category(), "open");
    }

    // 获取文件状态
    struct stat sb;
    if (fstat(fd_, &sb) == -1) {
        close(fd_);
        throw std::system_error(errno, std::system_category(), "fstat");
    }

    size_t file_size = sb.st_size;

    // 如果是空文件且非只读，则扩展为 total_size
    if (file_size == 0 && !read_only_) {
        if (ftruncate(fd_, total_size) == -1) {
            close(fd_);
            throw std::system_error(errno, std::system_category(), "ftruncate");
        }
        file_size = total_size;
    }

    // mmap 整个文件
    size_ = file_size;
    int prot = read_only_ ? PROT_READ : PROT_READ | PROT_WRITE;
    data_ = reinterpret_cast<uint8_t*>(::mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) {
        close(fd_);
        throw std::system_error(errno, std::system_category(), "mmap");
    }

    // 设置元信息字段和数据起始指针
    length_ptr_ = reinterpret_cast<uint64_t*>(data_);
    data_start_ = data_ + HEADER_SIZE;

    // 如果是非只读的新文件，初始化 length 为 0
    if (!read_only_ && sb.st_size == 0) {
        *length_ptr_ = 0;
    }

    // offset_ 从 header 中读取当前写入长度
    offset_ = *length_ptr_;
}

MMapFile::~MMapFile() {
    if (data_ != nullptr) {
        munmap(data_, size_);
        data_ = nullptr;
    }
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

MMapFile::MMapFile(MMapFile&& other) noexcept
    : path_(std::move(other.path_)),
      fd_(other.fd_),
      size_(other.size_),
      length_ptr_(other.length_ptr_),
      data_(other.data_),
      data_start_(other.data_start_),
      offset_(other.offset_),
      read_only_(other.read_only_) {
    other.fd_ = -1;
    other.data_ = nullptr;
    other.length_ptr_ = nullptr;
    other.data_start_ = nullptr;
    other.size_ = 0;
    other.offset_ = 0;
}

MMapFile& MMapFile::operator=(MMapFile&& other) noexcept {
    if (this != &other) {
        // 清理当前资源
        if (data_) {
            munmap(data_, size_);
            data_ = nullptr;
        }
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }

        // 移动资源
        path_ = std::move(other.path_);
        fd_ = other.fd_;
        size_ = other.size_;
        length_ptr_ = other.length_ptr_;
        data_ = other.data_;
        data_start_ = other.data_start_;
        offset_ = other.offset_;
        read_only_ = other.read_only_;

        // 置空原对象
        other.fd_ = -1;
        other.data_ = nullptr;
        other.length_ptr_ = nullptr;
        other.data_start_ = nullptr;
        other.size_ = 0;
        other.offset_ = 0;
    }
    return *this;
}

void MMapFile::expand(size_t needed_size) {
    if (size_ >= offset_ + needed_size + HEADER_SIZE) {
        return; // 不需要扩容
    }

    // 指数增长
    size_t new_size = size_;
    while (new_size < offset_ + needed_size + HEADER_SIZE) {
        new_size *= 2;
    }

    // 解除旧映射
    if (munmap(data_, size_) == -1) {
        throw std::system_error(errno, std::system_category(), "munmap");
    }

    // 扩展文件大小
    if (ftruncate(fd_, new_size) == -1) {
        throw std::system_error(errno, std::system_category(), "ftruncate");
    }

    // 重新映射
    data_ = reinterpret_cast<uint8_t*>(::mmap(nullptr, new_size,
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) {
        throw std::system_error(errno, std::system_category(), "mmap after expand");
    }

    // 更新指针
    length_ptr_ = reinterpret_cast<uint64_t*>(data_);
    data_start_ = data_ + HEADER_SIZE;
    size_ = new_size;

    std::cout << "Expanded mmap to " << new_size << " bytes" << std::endl;
}

void MMapFile::append(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (read_only_) {
        throw std::runtime_error("Cannot append to read-only mmap file");
    }

    if (offset_ + size > this->size_ - HEADER_SIZE) {
        expand(size);
    }

    memcpy(data_start_ + offset_, data, size);
    offset_ += size;
    *length_ptr_ = offset_; // 更新头部中的 length 字段
}

} // namespace tsdb