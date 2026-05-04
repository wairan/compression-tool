# Multithreaded Compression Engine

A university-grade, high-performance file compression tool written in C++17, demonstrating core Operating Systems concepts, coupled with a modern React + Express Web UI.

This project was built from the ground up to utilize **POSIX file mapping (`mmap`)**, **custom thread pools**, and **Shannon entropy analysis** to dynamically determine and execute the most efficient compression algorithm (Huffman or LZW) for any given file.

## 🚀 Key Features

* **Smart Algorithm Selector:** Computes Shannon entropy on the first 8 KB of the file. Automatically chooses **Huffman** for highly structured data (Entropy < 4.0) and **LZW** for mixed/binary data (Entropy ≥ 4.0).
* **Multithreaded Processing:** Slices files into 256 KB chunks and distributes them across a custom C++ Thread Pool. Uses `std::condition_variable` and RAII principles to eliminate busy-waiting and prevent data races.
* **Zero-Copy File I/O:** Leverages POSIX `mmap` for fast, zero-copy memory access to input files.
* **Custom Binary Formats:** Defines explicit little-endian byte formats (`CF` for single-thread, `PRLZ` for parallel) allowing seamless extraction and auto-detection on decompression.
* **Modern Web Interface:** A Vite/React frontend with drag-and-drop support, paired with a Node.js Express backend that safely streams the C++ executable's `stdout` and `stderr` using `child_process.spawn()`.

## 🧠 System Architecture

Read the full architecture documentation here: [ARCHITECTURE.md](docs/ARCHITECTURE.md).

### Tech Stack
* **Core Engine:** C++17 (POSIX standard)
* **Build System:** CMake
* **Frontend:** React, Vite, CSS Modules
* **Backend:** Node.js, Express, Multer

## 🛠️ Building & Running

### 1. Build the C++ Engine (Linux / WSL / macOS)
Because the engine uses POSIX-specific APIs (`mmap`, pthreads), it must be compiled in a POSIX-compliant environment.

```bash
# Clone the repository
git clone https://github.com/wairan/compression-tool.git
cd compression-tool

# Compile using CMake
mkdir build && cd build
cmake ..
make -j$(nproc)
```

**Running CLI Tests:**
```bash
./test_io
./test_algorithms
./test_parallel
```

### 2. Start the Web UI
The backend runs on port 3001 and the React frontend on port 3000. Ensure you have Node.js installed.

```bash
# From the project root, go to the UI directory
cd ui

# Install dependencies for both frontend and backend
npm install

# Start both development servers concurrently
npm run dev
```
Open your browser to `http://localhost:3000`. 

*(Note: If running on Windows, the Express backend automatically delegates the C++ executable execution through `wsl`).*

## 💻 CLI Usage

You can also use the C++ binary directly from the command line:

**Compress a file (Auto-selects algorithm):**
```bash
./compressor --input data.txt --output data.cmp --algo auto --threads 4 --verbose
```

**Decompress a file (Auto-detects algorithm from header):**
```bash
./compressor --input data.cmp --output restored.txt --decompress --threads 4 --verbose
```

### CLI Arguments:
* `--input <path>` : Input file path (required)
* `--output <path>` : Output file path (required)
* `--algo [huffman|lzw|auto]` : Algorithm selection (default: auto)
* `--decompress` : Decompress mode
* `--threads <N>` : Worker thread count (default: System CPU count)
* `--chunk-size <bytes>` : Chunk size for parallel execution (default: 1 MB)
* `--verbose` : Print entropy, timing, and compression ratio

## 📂 Project Structure

* `/src` - C++ source files
  * `/io` - File reading (`mmap`) and writing logic
  * `/algorithms` - Implementation of ICompressor (Huffman, LZW)
  * `/threading` - Custom ThreadPool and parallel execution engine
  * `/analysis` - Shannon Entropy analyzer
  * `/cli` - CLI parsing logic
* `/tests` - E2E Bash tests and isolated C++ unit tests
* `/ui` - Node.js Backend and React Frontend
* `/docs` - Architecture and system design documentation

## 🧪 Error Handling & Thread Safety
* Memory leaks are prevented natively through C++ smart pointers and RAII.
* Post-join chunk assembly is used to guarantee **zero shared mutable state** during the hot parallel compression loop.
* Built-in support for AddressSanitizer (ASan) and ThreadSanitizer (TSan) via CMake flags: `cmake -DSANITIZE_THREAD=ON ..`
## Link to the YouTube Video 
https://youtu.be/rqsmVIsQ97A
