# mini-TSDB

This project is a lightweight time series database storage engine built using **C++17**, **mmap**, and **FlatBuffers**, designed for IoT and monitoring use cases. It supports high-performance writes, time-range queries, and efficient compression using the **Delta-of-Delta** algorithm.

---

## 🚀 Features

- **Efficient Storage**: Uses `mmap` for memory-efficient file I/O.
- **Time Range Queries**: Supports querying data within specified timestamp ranges.
- **Delta-of-Delta Compression**: Compresses timestamps to reduce storage size.
- **FlatBuffers Integration**: Uses FlatBuffers for fast serialization/deserialization.
- **Modular Design**: Easy to extend with LSM Tree support or Prometheus remote write integration.

---

## 🧰 Technologies Used

- **C++17**: For modern features like smart pointers, concurrency, and templates.
- **mmap**: High-performance memory-mapped file operations.
- **FlatBuffers**: Efficient binary serialization format.
- **Delta-of-Delta Algorithm**: Optimized compression for time-series timestamps.

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
│       └── utils.h               // Utility functions
├── src/
│   ├── delta_delta.cpp           // Implementation of Delta-of-Delta algorithm
│   ├── mmap_file.cpp             // mmap file handling
│   ├── tsdb_writer.cpp           // Write interface implementation
│   └── main.cpp                  // Example usage
├── test/
│   ├── delta_delta_test.cpp      // Unit tests for Delta-of-Delta
│   ├── mmap_file_test.cpp        // Unit tests for mmap
│   └── writer_test.cpp           // Test for writing time series data
├── schema/
│   └── tsdb.fbs                  // FlatBuffers schema
├── benchmark/
│   └── write_benchmark.cpp       // Performance benchmarking
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
./test/delta_delta_test
./test/mmap_file_test
./test/writer_test
```

### 5. Run Benchmark

```bash
./benchmark/write_benchmark
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
