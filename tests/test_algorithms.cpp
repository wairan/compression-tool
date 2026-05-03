// ═══════════════════════════════════════════════════════════════════════
// test_algorithms.cpp — Round-trip tests for Huffman and LZW
//
// Self-contained: no external test framework required.
// Tests three input types through both algorithms:
//   1. All-zeros  (512 B)  — worst case for LZW, best for Huffman
//   2. Random bytes (4 KB) — stress test, near-incompressible
//   3. ASCII text  (1 KB)  — realistic use case
//
// Each test: compress → decompress → assert output == input byte-for-byte
// ═══════════════════════════════════════════════════════════════════════

#include "algorithms/HuffmanCompressor.hpp"
#include "algorithms/LZWCompressor.hpp"
#include "formats/CompressedFile.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdlib>    // srand, rand
#include <cstring>    // memcmp
#include <cassert>
#include <string>

// ─── Test result tracking ───────────────────────────────────────────
static int tests_passed = 0;
static int tests_failed = 0;

static void check(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "  ✓ PASS: " << testName << "\n";
        tests_passed++;
    } else {
        std::cout << "  ✗ FAIL: " << testName << "\n";
        tests_failed++;
    }
}

// ─── Generate test data ─────────────────────────────────────────────

static std::vector<uint8_t> generateZeros(size_t size) {
    return std::vector<uint8_t>(size, 0x00);
}

static std::vector<uint8_t> generateRandom(size_t size, unsigned seed = 42) {
    std::vector<uint8_t> data(size);
    srand(seed);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(rand() % 256);
    }
    return data;
}

static std::vector<uint8_t> generateAsciiText() {
    // ~1 KB of realistic English text with repeated patterns
    // (good for both Huffman frequency skew and LZW dictionary building)
    const char* text =
        "The operating system manages all hardware resources through a "
        "kernel that provides system calls for user-space programs. Memory "
        "management uses virtual addressing with page tables to map logical "
        "addresses to physical frames. The page replacement algorithm, "
        "such as LRU or Clock, decides which pages to evict when physical "
        "memory is full. Process scheduling determines which thread runs "
        "next on the CPU, balancing throughput and response time. The "
        "file system organizes data in inodes and directory entries, with "
        "journaling to prevent corruption after crashes. Inter-process "
        "communication mechanisms include pipes, shared memory, and "
        "message queues. Synchronization primitives like mutexes and "
        "semaphores prevent race conditions in concurrent programs. The "
        "virtual memory subsystem uses demand paging to load pages lazily, "
        "and copy-on-write to efficiently fork processes. Device drivers "
        "translate generic I/O requests into hardware-specific commands. "
        "The network stack implements TCP/IP protocols in layers, from "
        "the link layer up to the application layer.";

    return std::vector<uint8_t>(
        reinterpret_cast<const uint8_t*>(text),
        reinterpret_cast<const uint8_t*>(text) + strlen(text));
}

// ─── Round-trip test for one algorithm + one input ──────────────────

static void roundTripTest(ICompressor& algo,
                          const std::string& inputName,
                          const std::vector<uint8_t>& input)
{
    std::string label = algo.name() + " / " + inputName;

    // ── Compress ────────────────────────────────────────────────────
    std::vector<uint8_t> compressed;
    bool compOk = algo.compress(input.data(), input.size(), compressed);
    check(compOk, label + " — compression succeeds");
    if (!compOk) return;

    // ── Print compression ratio ─────────────────────────────────────
    double ratio = (input.size() > 0)
        ? static_cast<double>(compressed.size()) / input.size() * 100.0
        : 0.0;
    std::cout << "    Input: " << input.size() << " B → Compressed: "
              << compressed.size() << " B ("
              << std::fixed << std::setprecision(1) << ratio << "%)\n";

    // ── Decompress ──────────────────────────────────────────────────
    std::vector<uint8_t> decompressed;
    bool decOk = algo.decompress(compressed.data(), compressed.size(),
                                 decompressed);
    check(decOk, label + " — decompression succeeds");
    if (!decOk) return;

    // ── Verify byte-for-byte equality ───────────────────────────────
    bool sizeMatch = (decompressed.size() == input.size());
    check(sizeMatch, label + " — output size matches ("
          + std::to_string(decompressed.size()) + " vs "
          + std::to_string(input.size()) + ")");

    if (sizeMatch) {
        bool contentMatch = (memcmp(decompressed.data(), input.data(),
                                     input.size()) == 0);
        check(contentMatch, label + " — output content matches");
    }
}

// ─── Test CompressedFile wrapper ────────────────────────────────────

static void testCompressedFileWrapper() {
    std::cout << "\n=== CompressedFile Wrapper Tests ===" << std::endl;

    // Test wrap + unwrap round-trip
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};

    // Huffman wrap/unwrap
    auto wrapped = CompressedFile::wrap(CompressedFile::ALGO_HUFFMAN, payload);
    check(wrapped.size() == payload.size() + 4,
          "Wrap Huffman — correct size");

    auto result = CompressedFile::unwrap(wrapped.data(), wrapped.size());
    check(result.algoId == CompressedFile::ALGO_HUFFMAN,
          "Unwrap Huffman — correct algo ID");
    check(result.payloadSize == payload.size(),
          "Unwrap Huffman — correct payload size");
    check(memcmp(result.payload, payload.data(), payload.size()) == 0,
          "Unwrap Huffman — payload matches");

    // LZW wrap/unwrap
    wrapped = CompressedFile::wrap(CompressedFile::ALGO_LZW, payload);
    result = CompressedFile::unwrap(wrapped.data(), wrapped.size());
    check(result.algoId == CompressedFile::ALGO_LZW,
          "Unwrap LZW — correct algo ID");

    // Test error: unknown magic bytes
    std::vector<uint8_t> badMagic = {'X', 'Y', 0x01, 0x01};
    bool threwOnBadMagic = false;
    try {
        CompressedFile::unwrap(badMagic.data(), badMagic.size());
    } catch (const std::runtime_error& e) {
        threwOnBadMagic = true;
        std::cout << "    (Expected error: " << e.what() << ")\n";
    }
    check(threwOnBadMagic, "Unwrap rejects unknown magic bytes");

    // Test error: unknown algorithm ID
    std::vector<uint8_t> badAlgo = {'C', 'F', 0xFF, 0x01};
    bool threwOnBadAlgo = false;
    try {
        CompressedFile::unwrap(badAlgo.data(), badAlgo.size());
    } catch (const std::runtime_error& e) {
        threwOnBadAlgo = true;
        std::cout << "    (Expected error: " << e.what() << ")\n";
    }
    check(threwOnBadAlgo, "Unwrap rejects unknown algorithm ID");

    // Test error: data too small
    std::vector<uint8_t> tooSmall = {'C', 'F'};
    bool threwOnSmall = false;
    try {
        CompressedFile::unwrap(tooSmall.data(), tooSmall.size());
    } catch (const std::runtime_error& e) {
        threwOnSmall = true;
        std::cout << "    (Expected error: " << e.what() << ")\n";
    }
    check(threwOnSmall, "Unwrap rejects too-small data");

    // Test error: wrap with unknown algo ID
    bool threwOnWrap = false;
    try {
        CompressedFile::wrap(0xFF, payload);
    } catch (const std::invalid_argument& e) {
        threwOnWrap = true;
        std::cout << "    (Expected error: " << e.what() << ")\n";
    }
    check(threwOnWrap, "Wrap rejects unknown algorithm ID");
}

// ─── Main ───────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Algorithm Round-Trip Tests ===" << std::endl;

    // Generate test inputs
    auto zeros  = generateZeros(512);
    auto random = generateRandom(4096);
    auto text   = generateAsciiText();

    std::cout << "\nTest inputs:"
              << "\n  All-zeros:   " << zeros.size() << " bytes"
              << "\n  Random:      " << random.size() << " bytes"
              << "\n  ASCII text:  " << text.size() << " bytes"
              << std::endl;

    // ── Huffman tests ───────────────────────────────────────────────
    std::cout << "\n--- Huffman Coding ---" << std::endl;
    HuffmanCompressor huffman;
    roundTripTest(huffman, "all-zeros (512 B)", zeros);
    roundTripTest(huffman, "random (4 KB)",     random);
    roundTripTest(huffman, "ASCII text (1 KB)", text);

    // ── LZW tests ───────────────────────────────────────────────────
    std::cout << "\n--- LZW ---" << std::endl;
    LZWCompressor lzw;
    roundTripTest(lzw, "all-zeros (512 B)", zeros);
    roundTripTest(lzw, "random (4 KB)",     random);
    roundTripTest(lzw, "ASCII text (1 KB)", text);

    // ── CompressedFile wrapper tests ────────────────────────────────
    testCompressedFileWrapper();

    // ── Summary ─────────────────────────────────────────────────────
    std::cout << "\n=============================="
              << "\n  Passed: " << tests_passed
              << "\n  Failed: " << tests_failed
              << "\n==============================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
