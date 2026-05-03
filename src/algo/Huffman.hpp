// ═══════════════════════════════════════════════════════════════════════
// Huffman.hpp — Huffman Coding compression/decompression
// Stub for Step 2. Will implement:
//   - Frequency analysis of input bytes
//   - Huffman tree construction (priority queue / min-heap)
//   - Canonical code generation for compact tree serialization
//   - Bitstream encoder (compression)
//   - Tree + bitstream decoder (decompression)
// ═══════════════════════════════════════════════════════════════════════
#ifndef HUFFMAN_HPP
#define HUFFMAN_HPP

#include <cstdint>
#include <vector>

namespace algo {

class Huffman {
public:
    // Compress input data, return compressed bytes
    static std::vector<uint8_t> compress(const uint8_t* data, size_t size);

    // Decompress previously compressed data
    static std::vector<uint8_t> decompress(const uint8_t* data, size_t size);
};

} // namespace algo

#endif // HUFFMAN_HPP
