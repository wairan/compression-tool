// ═══════════════════════════════════════════════════════════════════════
// test_parallel.cpp — Tests for ThreadPool and ParallelCompressor
//
// Self-contained: no external test framework.
// Tests:
//   1. Round-trip: 2 MB zeros, 4 threads, verify byte-for-byte
//   2. Thread scaling: same buffer with 1, 2, 4, 8 threads — all must
//      produce identical decompressed output. Wall-clock timing printed.
//   3. Small input: buffer smaller than one chunk — single-chunk fallback
//
// Build with sanitizers for extra safety:
//   g++ -std=c++17 -Wall -Wextra -fsanitize=address,thread -g ...
//   (Note: -fsanitize=address and -fsanitize=thread are mutually
//    exclusive — run them as separate builds.)
// ═══════════════════════════════════════════════════════════════════════

#include "threading/ThreadPool.hpp"
#include "threading/ParallelCompressor.hpp"
#include "algorithms/HuffmanCompressor.hpp"
#include "algorithms/LZWCompressor.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <cstring>    // memcmp
#include <cassert>

static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "  \xe2\x9c\x93 PASS: " << testName << "\n";
        tests_passed++;
    } else {
        std::cout << "  \xe2\x9c\x97 FAIL: " << testName << "\n";
        tests_failed++;
    }
}

// ─── Test 1: Basic round-trip (2 MB zeros, 4 threads) ───────────────

static void testRoundTrip() {
    std::cout << "\n=== Test 1: Round-trip (2 MB zeros, 4 threads) ===" << std::endl;

    // 2 MB of zeros — highly compressible
    const size_t SIZE = 2 * 1024 * 1024;
    std::vector<uint8_t> input(SIZE, 0x00);

    HuffmanCompressor huffman;
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> decompressed;

    // Compress with 4 threads, 256 KB chunks
    bool compOk = ParallelCompressor::compress(
        huffman, 0x01,
        input.data(), input.size(),
        compressed,
        256 * 1024,  // chunkSize
        4            // numThreads
    );
    check(compOk, "Compression succeeds");

    std::cout << "    Input: " << input.size() << " bytes -> Compressed: "
              << compressed.size() << " bytes ("
              << std::fixed << std::setprecision(1)
              << (100.0 * compressed.size() / input.size()) << "%)\n";

    // Decompress with 4 threads
    bool decOk = ParallelCompressor::decompress(
        huffman,
        compressed.data(), compressed.size(),
        decompressed,
        4
    );
    check(decOk, "Decompression succeeds");

    check(decompressed.size() == input.size(),
          "Output size matches (" + std::to_string(decompressed.size()) +
          " vs " + std::to_string(input.size()) + ")");

    if (decompressed.size() == input.size()) {
        check(memcmp(decompressed.data(), input.data(), input.size()) == 0,
              "Output content matches byte-for-byte");
    }
}

// ─── Test 2: Thread scaling with timing ─────────────────────────────

static void testThreadScaling() {
    std::cout << "\n=== Test 2: Thread scaling (2 MB zeros, 1/2/4/8 threads) ==="
              << std::endl;

    const size_t SIZE = 2 * 1024 * 1024;
    std::vector<uint8_t> input(SIZE, 0x00);

    LZWCompressor lzw;
    const int threadCounts[] = {1, 2, 4, 8};
    std::vector<std::vector<uint8_t>> allDecompressed(4);

    // Table header
    std::cout << "\n    " << std::left
              << std::setw(10) << "Threads"
              << std::setw(18) << "Compress (ms)"
              << std::setw(18) << "Decompress (ms)"
              << std::setw(18) << "Compressed Size"
              << "\n    "
              << std::string(64, '-') << "\n";

    for (int t = 0; t < 4; ++t) {
        size_t nThreads = threadCounts[t];

        // ── Compress with timing ────────────────────────────────────
        std::vector<uint8_t> compressed;
        auto t0 = std::chrono::high_resolution_clock::now();

        bool compOk = ParallelCompressor::compress(
            lzw, 0x02,
            input.data(), input.size(),
            compressed,
            256 * 1024,
            nThreads
        );

        auto t1 = std::chrono::high_resolution_clock::now();
        double compMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        check(compOk, std::to_string(nThreads) + " thread(s) — compression");

        // ── Decompress with timing ──────────────────────────────────
        auto t2 = std::chrono::high_resolution_clock::now();

        bool decOk = ParallelCompressor::decompress(
            lzw,
            compressed.data(), compressed.size(),
            allDecompressed[t],
            nThreads
        );

        auto t3 = std::chrono::high_resolution_clock::now();
        double decMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

        check(decOk, std::to_string(nThreads) + " thread(s) — decompression");

        // Print timing row
        std::cout << "    " << std::left
                  << std::setw(10) << nThreads
                  << std::setw(18) << std::fixed << std::setprecision(2) << compMs
                  << std::setw(18) << std::fixed << std::setprecision(2) << decMs
                  << std::setw(18) << compressed.size()
                  << "\n";
    }

    // ── Verify all thread counts produce identical output ───────────
    std::cout << "\n";
    for (int t = 1; t < 4; ++t) {
        bool sizeMatch = (allDecompressed[t].size() == input.size());
        bool contentMatch = sizeMatch &&
            (memcmp(allDecompressed[t].data(), input.data(), input.size()) == 0);

        check(contentMatch,
              std::to_string(threadCounts[t]) +
              " threads — output matches original");
    }

    // Also verify 1-thread output
    check(allDecompressed[0].size() == input.size() &&
          memcmp(allDecompressed[0].data(), input.data(), input.size()) == 0,
          "1 thread — output matches original");
}

// ─── Test 3: Small input (< 1 chunk) ────────────────────────────────

static void testSmallInput() {
    std::cout << "\n=== Test 3: Small input (100 bytes, chunk=256KB) ==="
              << std::endl;

    // 100 bytes — much smaller than the 256 KB chunk size.
    // This should result in exactly 1 chunk with no special-case code.
    std::vector<uint8_t> input(100);
    for (int i = 0; i < 100; ++i) {
        input[i] = static_cast<uint8_t>(i % 26 + 'a');  // "abcdef..."
    }

    HuffmanCompressor huffman;
    std::vector<uint8_t> compressed;
    std::vector<uint8_t> decompressed;

    bool compOk = ParallelCompressor::compress(
        huffman, 0x01,
        input.data(), input.size(),
        compressed,
        256 * 1024,  // chunkSize >> input size
        4            // numThreads
    );
    check(compOk, "Small input — compression succeeds");

    std::cout << "    Input: " << input.size() << " bytes -> Compressed: "
              << compressed.size() << " bytes\n";

    bool decOk = ParallelCompressor::decompress(
        huffman,
        compressed.data(), compressed.size(),
        decompressed,
        4
    );
    check(decOk, "Small input — decompression succeeds");

    check(decompressed.size() == input.size(),
          "Small input — size matches");

    if (decompressed.size() == input.size()) {
        check(memcmp(decompressed.data(), input.data(), input.size()) == 0,
              "Small input — content matches");
    }
}

// ─── Test 4: ThreadPool basic functionality ─────────────────────────

static void testThreadPoolBasics() {
    std::cout << "\n=== Test 4: ThreadPool basics ===" << std::endl;

    // Test that tasks execute and futures resolve
    {
        ThreadPool pool(2);
        std::atomic<int> counter{0};

        std::vector<std::future<void>> futures;
        for (int i = 0; i < 10; ++i) {
            futures.push_back(pool.enqueue([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            }));
        }

        for (auto& f : futures) { f.get(); }

        check(counter.load() == 10,
              "10 tasks executed (counter = " +
              std::to_string(counter.load()) + ")");
    }
    // Pool destructor called here — should not hang

    check(true, "ThreadPool destructor did not hang");

    // Test shutdown idempotency
    {
        ThreadPool pool(2);
        pool.shutdown();
        pool.shutdown();  // Second call should be harmless
    }
    check(true, "Double shutdown did not hang");

    // Test enqueue after shutdown throws
    {
        ThreadPool pool(1);
        pool.shutdown();
        bool threw = false;
        try {
            pool.enqueue([]() {});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "Enqueue after shutdown throws");
    }
}

// ─── Main ───────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Parallel Compression Tests ===" << std::endl;

    testThreadPoolBasics();
    testRoundTrip();
    testThreadScaling();
    testSmallInput();

    std::cout << "\n=============================="
              << "\n  Passed: " << tests_passed
              << "\n  Failed: " << tests_failed
              << "\n==============================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
