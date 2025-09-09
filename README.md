# mini-TSDB

This project is a lightweight time series database storage engine built using **C++17**, **mmap**, and **FlatBuffers**, designed for IoT and monitoring use cases. It supports high-performance writes, time-range queries, and efficient compression using the **Delta-of-Delta** algorithm.

---

## 🚀 Features
- **Sharded Architecture**: Distributes writes across multiple shards for high concurrency
- **Hybrid Compression**: Combines Delta-of-Delta for timestamps with raw value storage
- **Write-Ahead Log**: Atomic writes with per-shard recovery points
- **Batch Processing**: Optimized for both single and bulk writes
- **Lock-Free Queues**: Minimizes contention between producer/consumer threads

---

## 🧰 Technologies Used
- **C++20**: Leverages modern features like atomic smart pointers
- **Memory Mapped Files**: Zero-copy file operations via `mmap`
- **FlatBuffers v2.0**: Schema-based binary serialization
- **Boost.Lockfree**: High-performance concurrent queues
- **Delta-Delta Encoding**: Compresses timestamps with zigzag varint

---

## 📁 Project Structure

```
mini-TSDB/
├── CMakeLists.txt
├── README.md
├── include/
│   └── tsdb/
│       ├── delta_delta.h         // Delta-of-Delta encoder/decoder
│       ├── mmap_file.h           // Memory-mapped file management
│       ├── tsdb_writer.h         // TSDB write interface
│       ├── tsdb_reader.h         // TSDB read interface
|       ├── wal.h                 // Write-ahead log
│       └── utils.h               // Utility functions
├── src/
│   ├── delta_delta.cpp           // Implementation of Delta-of-Delta algorithm
│   ├── mmap_file.cpp             // mmap file handling
│   ├── tsdb_writer.cpp           // Write interface implementation
│   ├── tsdb_reader.cpp           // Read interface implementation
|   ├── wal.h                     // Write-ahead log
│   └── main.cpp                  // Example usage
├── test/
│   ├── delta_delta_test.cpp      // Unit tests for Delta-of-Delta
│   ├── mmap_file_test.cpp        // Unit tests for mmap
│   ├── tsdb_writer_test.cpp      // Test for writing time series data
│   ├── tsdb_reader_test.cpp      // Test for reading time series data
|   └── wal_test.cpp              // Test for Write-ahead log
├── schema/
│   └── tsdb.fbs                  // FlatBuffers schema
├── benchmark/
│   └── tsdb_benchmark.cpp        // Performance benchmarking
├── build/                        // Build output directory
└── scripts/
    └── generate_flatbuffers.sh   // Script to generate FlatBuffers code
```

---

## 🛠️ Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/kevin3227/mini-TSDB.git
cd mini-TSDB
```

### 2. Generate FlatBuffers Code (Optional)

If you modify the schema (`schema/tsdb.fbs`), run:

```bash
./scripts/generate_flatbuffers.sh
```

### 3. Build the Project

```bash
mkdir build && cd build
cmake ..
make
```

### 4. Run Tests

```bash
./test/[MODULE_NAME]_test
```

### 5. Run Benchmark

```bash
./benchmark/benchmark
```

---

## 🧩 Future Enhancements

- [ ] Add Prometheus Remote Write protocol support
- [ ] Implement LSM Tree-based persistence layer
- [ ] Integrate eBPF for automatic metric collection
- [ ] Support multiple time series per file (by ID/tags)
- [ ] Add background compaction and garbage collection

---

## 📄 License

MIT License – see [LICENSE](LICENSE) for details.
