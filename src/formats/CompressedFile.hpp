#ifndef COMPRESSED_FILE_HPP
#define COMPRESSED_FILE_HPP

// ═══════════════════════════════════════════════════════════════════════
// CompressedFile — Outer container format for compressed data
//
// Wraps either Huffman or LZW algorithm output with a uniform envelope
// so the decompressor can identify which algorithm to use without
// parsing the inner algorithm-specific header.
//
// Outer format:
//   Offset  Size  Field
//   ──────  ────  ──────────────────────────────────────────────────
//   0       2     Outer magic "CF" (Compressed File)
//   2       1     Algorithm ID: 0x01 = Huffman, 0x02 = LZW
//   3       1     Version: 0x01
//   4       ...   Raw algorithm payload (contains its own inner header)
//
// The inner payload is opaque to CompressedFile — it just wraps/unwraps.
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>     // memcmp
#include <stdexcept>   // std::runtime_error

class CompressedFile {
public:
    // Algorithm IDs (match the spec)
    static constexpr uint8_t ALGO_HUFFMAN = 0x01;
    static constexpr uint8_t ALGO_LZW     = 0x02;

    // Current version
    static constexpr uint8_t VERSION = 0x01;

    // Outer header size in bytes
    static constexpr size_t HEADER_SIZE = 4;  // "CF" + algoId + version

    // ── Result of unwrap ────────────────────────────────────────────
    struct UnwrapResult {
        uint8_t        algoId;       // ALGO_HUFFMAN or ALGO_LZW
        const uint8_t* payload;      // Pointer into the original data
        size_t         payloadSize;  // Size of payload in bytes
    };

    // ─────────────────────────────────────────────────────────────────
    // wrap(): Wraps an algorithm payload with the outer container header.
    //
    // Parameters:
    //   algoId  — ALGO_HUFFMAN (0x01) or ALGO_LZW (0x02)
    //   payload — the raw compressed data from the algorithm
    //
    // Returns: vector containing [CF header][payload]
    // Throws: std::invalid_argument for unknown algorithm IDs.
    // ─────────────────────────────────────────────────────────────────
    static std::vector<uint8_t> wrap(uint8_t algoId,
                                     const std::vector<uint8_t>& payload)
    {
        // Validate algorithm ID
        if (algoId != ALGO_HUFFMAN && algoId != ALGO_LZW) {
            throw std::invalid_argument(
                "CompressedFile::wrap: Unknown algorithm ID 0x" +
                toHex(algoId) + ". Expected 0x01 (Huffman) or 0x02 (LZW).");
        }

        std::vector<uint8_t> result;
        result.reserve(HEADER_SIZE + payload.size());

        // Outer magic "CF"
        result.push_back('C');
        result.push_back('F');

        // Algorithm ID
        result.push_back(algoId);

        // Version
        result.push_back(VERSION);

        // Append the raw algorithm payload
        result.insert(result.end(), payload.begin(), payload.end());

        return result;
    }

    // ─────────────────────────────────────────────────────────────────
    // unwrap(): Validates the outer header and extracts the payload.
    //
    // Parameters:
    //   data — pointer to the full compressed file contents
    //   size — total size in bytes
    //
    // Returns: UnwrapResult with algoId and a view into the payload.
    // Throws: std::runtime_error with a descriptive message if:
    //   - Data is too small for the header
    //   - Magic bytes don't match "CF"
    //   - Algorithm ID is unknown
    //   - Version is unsupported
    // ─────────────────────────────────────────────────────────────────
    static UnwrapResult unwrap(const uint8_t* data, size_t size)
    {
        // Check minimum size
        if (size < HEADER_SIZE) {
            throw std::runtime_error(
                "CompressedFile::unwrap: Data too small (" +
                std::to_string(size) + " bytes, need at least " +
                std::to_string(HEADER_SIZE) + ").");
        }

        // Validate outer magic "CF"
        if (data[0] != 'C' || data[1] != 'F') {
            throw std::runtime_error(
                "CompressedFile::unwrap: Invalid magic bytes (expected 'CF', "
                "got '" + std::string(1, static_cast<char>(data[0])) +
                std::string(1, static_cast<char>(data[1])) + "').");
        }

        uint8_t algoId  = data[2];
        uint8_t version = data[3];

        // Validate algorithm ID
        if (algoId != ALGO_HUFFMAN && algoId != ALGO_LZW) {
            throw std::runtime_error(
                "CompressedFile::unwrap: Unknown algorithm ID 0x" +
                toHex(algoId) +
                ". Supported: 0x01 (Huffman), 0x02 (LZW).");
        }

        // Validate version
        if (version != VERSION) {
            throw std::runtime_error(
                "CompressedFile::unwrap: Unsupported version " +
                std::to_string(version) +
                " (only version " + std::to_string(VERSION) + " is supported).");
        }

        return UnwrapResult{
            algoId,
            data + HEADER_SIZE,
            size - HEADER_SIZE
        };
    }

    // ── Utility: get algorithm name from ID ─────────────────────────
    static std::string algoName(uint8_t algoId) {
        switch (algoId) {
            case ALGO_HUFFMAN: return "Huffman";
            case ALGO_LZW:    return "LZW";
            default:           return "Unknown(0x" + toHex(algoId) + ")";
        }
    }

private:
    // Convert a byte to a 2-character hex string
    static std::string toHex(uint8_t byte) {
        const char* hex = "0123456789ABCDEF";
        return std::string(1, hex[byte >> 4]) +
               std::string(1, hex[byte & 0x0F]);
    }
};

#endif // COMPRESSED_FILE_HPP
