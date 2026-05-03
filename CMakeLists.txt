cmake_minimum_required(VERSION 3.16)
project(FileCompressor VERSION 1.0.0 LANGUAGES CXX)

# ──────────────────────────────────────────────────────────────────────
# Global compiler settings
#   -O2        : Optimized build (good balance of speed vs debug-ability)
#   -Wall      : Enable all common warnings
#   -Wextra    : Enable extra warnings beyond -Wall
#   -std=c++17 : We need std::optional, std::filesystem, structured bindings
# ──────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-O2 -Wall -Wextra)

# ──────────────────────────────────────────────────────────────────────
# Optional sanitizers (for testing thread safety and memory correctness)
#   cmake -DSANITIZE_ADDRESS=ON ..   → AddressSanitizer (memory errors)
#   cmake -DSANITIZE_THREAD=ON ..    → ThreadSanitizer (data races)
# NOTE: ASan and TSan are MUTUALLY EXCLUSIVE — use one at a time.
# ──────────────────────────────────────────────────────────────────────
option(SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
option(SANITIZE_THREAD "Enable ThreadSanitizer" OFF)

if(SANITIZE_ADDRESS)
    add_compile_options(-fsanitize=address -g)
    add_link_options(-fsanitize=address)
endif()

if(SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
endif()

# ──────────────────────────────────────────────────────────────────────
# Platform check — this project requires POSIX mmap / pthreads
# ──────────────────────────────────────────────────────────────────────
if(WIN32)
    message(WARNING "This project targets POSIX (Linux/macOS). "
                    "Windows builds will require WSL or equivalent.")
endif()

# ──────────────────────────────────────────────────────────────────────
# pthreads — required for the multithreaded compression engine (Step 3)
# ──────────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ──────────────────────────────────────────────────────────────────────
# Source files — organized by concern
# ──────────────────────────────────────────────────────────────────────

# --- I/O layer (Step 1) ---
set(IO_SOURCES
    src/io/MmapReader.cpp
    src/io/FileWriter.cpp
)

# --- Compression algorithms (Step 2) ---
set(ALGO_SOURCES
    src/algorithms/HuffmanCompressor.cpp
    src/algorithms/LZWCompressor.cpp
)

# --- Threading layer (Step 3) ---
set(THREADING_SOURCES
    src/threading/ThreadPool.cpp
    src/threading/ParallelCompressor.cpp
)

# --- Analysis layer (Step 4) ---
set(ANALYSIS_SOURCES
    src/analysis/EntropyAnalyzer.cpp
)

# --- CLI (Step 4) ---
set(CLI_SOURCES
    src/main.cpp
    src/cli/CLI.cpp
)

# ──────────────────────────────────────────────────────────────────────
# Main executable
# ──────────────────────────────────────────────────────────────────────
add_executable(compressor
    ${CLI_SOURCES}
    ${IO_SOURCES}
    ${ALGO_SOURCES}
    ${THREADING_SOURCES}
    ${ANALYSIS_SOURCES}
)

target_include_directories(compressor PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(compressor PRIVATE Threads::Threads)

# ──────────────────────────────────────────────────────────────────────
# Test binary for I/O layer (Step 1 deliverable)
# ──────────────────────────────────────────────────────────────────────
add_executable(test_io
    tests/test_io.cpp
    ${IO_SOURCES}
)

target_include_directories(test_io PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_io PRIVATE Threads::Threads)

# ──────────────────────────────────────────────────────────────────────
# Test binary for algorithms (Step 2 deliverable)
# ──────────────────────────────────────────────────────────────────────
add_executable(test_algorithms
    tests/test_algorithms.cpp
    ${ALGO_SOURCES}
)

target_include_directories(test_algorithms PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_algorithms PRIVATE Threads::Threads)

# ──────────────────────────────────────────────────────────────────────
# Test binary for parallel compression (Step 3 deliverable)
# ──────────────────────────────────────────────────────────────────────
add_executable(test_parallel
    tests/test_parallel.cpp
    ${ALGO_SOURCES}
    ${THREADING_SOURCES}
)

target_include_directories(test_parallel PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_parallel PRIVATE Threads::Threads)
