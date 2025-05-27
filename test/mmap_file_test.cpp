#include <gtest/gtest.h>
#include "tsdb/mmap_file.h"
#include <cstring>

TEST(MMapFileTest, WriteAndReadBack) {
    const std::string filename = "test_mmap.bin";
    const char* test_data = "Hello, mmap!";
    size_t data_len = strlen(test_data);

    // 创建并写入 mmap 文件
    {
        tsdb::MMapFile mm(filename, 4096, false);
        mm.append(test_data, data_len);
    }

    // 重新打开文件并读取
    {
        tsdb::MMapFile mm(filename, 4096, true);  // 只读模式
        const uint8_t* data = mm.data();
        EXPECT_EQ(memcmp(data, test_data, data_len), 0);
        EXPECT_EQ(mm.length(), data_len);
    }

    std::remove(filename.c_str());  // 清理测试文件
}

TEST(MMapFileTest, ExpandTest) {
    const std::string filename = "test_expand.bin";

    {
        tsdb::MMapFile mm(filename, 4096, false);
        for (int i = 0; i < 1000; ++i) {
            mm.append("A", 1);
        }
        EXPECT_GT(mm.size(), 4096);  // 确保扩容成功
    }

    std::remove(filename.c_str());  // 清理测试文件
}