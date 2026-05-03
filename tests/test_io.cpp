// ═══════════════════════════════════════════════════════════════════════
// test_io.cpp — Step 1 validation test
//
// Purpose: Proves that MmapReader and FileWriter work correctly before
//          we build compression algorithms on top of them.
//
// Usage:
//   ./test_io <filepath>
//
// The test will:
//   1. mmap the given file via MmapReader
//   2. Print the first 64 bytes (or fewer) as a hex dump
//   3. Copy those bytes to "test_output.bin" via FileWriter
//   4. Verify the copy matches the original
// ═══════════════════════════════════════════════════════════════════════

#include "io/MmapReader.hpp"
#include "io/FileWriter.hpp"

#include <iostream>
#include <iomanip>    // std::hex, std::setw, std::setfill
#include <cstring>    // memcmp
#include <algorithm>  // std::min

// ─────────────────────────────────────────────────────────────────────
// hexDump: Prints `length` bytes from `data` in a readable hex format.
//          16 bytes per row, with ASCII printable characters on the side.
// ─────────────────────────────────────────────────────────────────────
static void hexDump(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; i += 16) {
        // Print offset
        std::cout << std::hex << std::setw(8) << std::setfill('0') << i << "  ";

        // Print hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < length) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(data[i + j]) << " ";
            } else {
                std::cout << "   ";  // Padding for incomplete rows
            }
            if (j == 7) std::cout << " ";  // Extra space between groups of 8
        }

        // Print ASCII representation
        std::cout << " |";
        for (size_t j = 0; j < 16 && (i + j) < length; ++j) {
            char c = static_cast<char>(data[i + j]);
            std::cout << (c >= 32 && c < 127 ? c : '.');
        }
        std::cout << "|" << std::endl;
    }

    // Reset to decimal for subsequent output
    std::cout << std::dec;
}

int main(int argc, char* argv[]) {
    // ── Argument validation ─────────────────────────────────────────
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <filepath>\n";
        std::cerr << "  Reads the file via mmap and prints first 64 bytes as hex.\n";
        return 1;
    }

    const std::string input_path = argv[1];
    const std::string output_path = "test_output.bin";

    // ── Test 1: MmapReader ──────────────────────────────────────────
    std::cout << "=== Test 1: MmapReader ===" << std::endl;
    try {
        MmapReader reader(input_path);

        std::cout << "File:   " << reader.filepath() << std::endl;
        std::cout << "Size:   " << reader.size() << " bytes" << std::endl;

        if (reader.size() == 0) {
            std::cout << "  (empty file — no hex dump)\n";
            std::cout << "  PASS: Empty file handled gracefully.\n";
            return 0;
        }

        // Print the first 64 bytes (or the entire file if < 64 bytes)
        size_t dump_size = std::min(reader.size(), static_cast<size_t>(64));
        std::cout << "\nFirst " << dump_size << " bytes:\n";
        hexDump(reader.data(), dump_size);

        std::cout << "\n  PASS: MmapReader created and data accessible.\n";

        // ── Test 2: FileWriter ──────────────────────────────────────
        std::cout << "\n=== Test 2: FileWriter ===" << std::endl;
        {
            FileWriter writer(output_path);

            // Write the first 64 bytes (or fewer) to the output file
            bool ok = writer.write(reader.data(), dump_size);
            if (!ok) {
                std::cerr << "  FAIL: FileWriter::write returned false.\n";
                return 1;
            }

            ok = writer.flush();
            if (!ok) {
                std::cerr << "  FAIL: FileWriter::flush returned false.\n";
                return 1;
            }

            std::cout << "  Wrote " << writer.bytesWritten()
                      << " bytes to '" << writer.filepath() << "'\n";
            std::cout << "  PASS: FileWriter wrote and flushed successfully.\n";
        } // FileWriter destructor closes the file here

        // ── Test 3: Verify the copy ─────────────────────────────────
        std::cout << "\n=== Test 3: Verify copy ===" << std::endl;
        {
            MmapReader verify(output_path);

            if (verify.size() != dump_size) {
                std::cerr << "  FAIL: Output size " << verify.size()
                          << " != expected " << dump_size << "\n";
                return 1;
            }

            if (memcmp(reader.data(), verify.data(), dump_size) != 0) {
                std::cerr << "  FAIL: Output content does not match input.\n";
                return 1;
            }

            std::cout << "  Output matches input (" << dump_size << " bytes).\n";
            std::cout << "  PASS: Round-trip verified.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}
