#ifndef ICOMPRESSOR_HPP
#define ICOMPRESSOR_HPP

// ═══════════════════════════════════════════════════════════════════════
// ICompressor — Abstract interface for compression algorithms
//
// Design rationale: Both Huffman and LZW implement this interface so the
// engine (Step 3) can treat them polymorphically. The interface operates
// on raw byte buffers — no file I/O here. Separation of concerns:
//   - Algorithms transform bytes → bytes
//   - I/O layer (MmapReader/FileWriter) handles disk access
//   - Engine orchestrates the pipeline
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <vector>
#include <string>

class ICompressor {
public:
    // Compress input bytes into the output buffer.
    // Returns true on success, false on error.
    // The output vector is cleared and filled with compressed data
    // (including the algorithm-specific header).
    virtual bool compress(const uint8_t* in, size_t inSize,
                          std::vector<uint8_t>& out) = 0;

    // Decompress input bytes into the output buffer.
    // Returns true on success, false on error.
    // The input is expected to contain the algorithm-specific header.
    virtual bool decompress(const uint8_t* in, size_t inSize,
                            std::vector<uint8_t>& out) = 0;

    // Human-readable algorithm name (e.g., "Huffman", "LZW").
    virtual std::string name() const = 0;

    // Virtual destructor for proper polymorphic cleanup.
    virtual ~ICompressor() = default;
};

#endif // ICOMPRESSOR_HPP
