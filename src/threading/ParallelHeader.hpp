#ifndef PARALLEL_HEADER_HPP
#define PARALLEL_HEADER_HPP

// ═══════════════════════════════════════════════════════════════════════
// ParallelHeader — Binary layout for parallel-compressed output
//
// All multi-byte fields are LITTLE-ENDIAN.
// Rationale: x86/x86-64 (the dominant desktop architecture) is natively
// little-endian, so we avoid byte-swap overhead on the most common
// target. ARM (Android, Apple Silicon) supports both endiannesses but
// runs little-endian by default in Linux/macOS userspace.
//
// Binary format:
//   Offset  Size  Field                Description
//   ──────  ────  ───────────────────  ────────────────────────────────
//   0       4     magic                "PRLZ" (Parallel Zip)
//   4       1     algoId               0x01=Huffman, 0x02=LZW
//   5       4     chunkCount           Number of chunks (uint32_t LE)
//   9       4     chunkSize            Original (uncompressed) chunk
//                                      size in bytes (uint32_t LE).
//                                      Last chunk may be smaller.
//   13      8     totalOriginalSize    Total uncompressed size (uint64_t LE)
//   21      ...   chunk data           Repeated chunkCount times:
//                                        4 bytes: compressed chunk size (uint32_t LE)
//                                        N bytes: compressed chunk data
//
// Total fixed header size: 21 bytes
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstring>    // memcpy, memcmp
#include <vector>

struct ParallelHeader {
    static constexpr char     MAGIC[4]    = {'P', 'R', 'L', 'Z'};
    static constexpr size_t   HEADER_SIZE = 21;

    uint8_t  algoId;
    uint32_t chunkCount;
    uint32_t chunkSize;         // Original uncompressed chunk size
    uint64_t totalOriginalSize;

    // ── Serialize header to bytes (little-endian) ───────────────────
    void writeTo(std::vector<uint8_t>& out) const {
        size_t start = out.size();
        out.resize(start + HEADER_SIZE);
        uint8_t* p = out.data() + start;

        // Magic bytes
        memcpy(p, MAGIC, 4);
        p += 4;

        // Algorithm ID
        *p++ = algoId;

        // Chunk count (little-endian uint32_t)
        // Little-endian: least significant byte first
        p[0] = static_cast<uint8_t>((chunkCount >>  0) & 0xFF);
        p[1] = static_cast<uint8_t>((chunkCount >>  8) & 0xFF);
        p[2] = static_cast<uint8_t>((chunkCount >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((chunkCount >> 24) & 0xFF);
        p += 4;

        // Chunk size (little-endian uint32_t)
        p[0] = static_cast<uint8_t>((chunkSize >>  0) & 0xFF);
        p[1] = static_cast<uint8_t>((chunkSize >>  8) & 0xFF);
        p[2] = static_cast<uint8_t>((chunkSize >> 16) & 0xFF);
        p[3] = static_cast<uint8_t>((chunkSize >> 24) & 0xFF);
        p += 4;

        // Total original size (little-endian uint64_t)
        for (int i = 0; i < 8; ++i) {
            p[i] = static_cast<uint8_t>((totalOriginalSize >> (i * 8)) & 0xFF);
        }
    }

    // ── Deserialize header from bytes ───────────────────────────────
    // Returns true on success, false if magic doesn't match or data too small.
    bool readFrom(const uint8_t* data, size_t dataSize) {
        if (dataSize < HEADER_SIZE) return false;
        if (memcmp(data, MAGIC, 4) != 0) return false;

        const uint8_t* p = data + 4;

        algoId = *p++;

        // Read little-endian uint32_t
        chunkCount = static_cast<uint32_t>(p[0])
                   | (static_cast<uint32_t>(p[1]) << 8)
                   | (static_cast<uint32_t>(p[2]) << 16)
                   | (static_cast<uint32_t>(p[3]) << 24);
        p += 4;

        chunkSize = static_cast<uint32_t>(p[0])
                  | (static_cast<uint32_t>(p[1]) << 8)
                  | (static_cast<uint32_t>(p[2]) << 16)
                  | (static_cast<uint32_t>(p[3]) << 24);
        p += 4;

        totalOriginalSize = 0;
        for (int i = 0; i < 8; ++i) {
            totalOriginalSize |= static_cast<uint64_t>(p[i]) << (i * 8);
        }

        return true;
    }

    // ── Utility: write a little-endian uint32_t to a vector ─────────
    static void writeLE32(std::vector<uint8_t>& out, uint32_t val) {
        out.push_back(static_cast<uint8_t>((val >>  0) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >>  8) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    // ── Utility: read a little-endian uint32_t from a pointer ───────
    static uint32_t readLE32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    }
};

#endif // PARALLEL_HEADER_HPP
