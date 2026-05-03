#ifndef LZW_COMPRESSOR_HPP
#define LZW_COMPRESSOR_HPP

// ═══════════════════════════════════════════════════════════════════════
// LZWCompressor — Lempel-Ziv-Welch compression/decompression
//
// Uses 12-bit fixed-width codes (max dictionary: 4096 entries).
// The first 256 entries are pre-initialized with single-byte values.
// Codes 256–4095 are assigned to multi-byte strings found during
// compression. When the dictionary fills, no new entries are added
// (the existing dictionary continues to be used).
//
// Output format:
//   [0..3]   4 bytes: magic "LZW_"
//   [4..5]   2 bytes: max code width in bits (uint16_t, big-endian) = 12
//   [6..13]  8 bytes: original uncompressed size (uint64_t, big-endian)
//   [14..]   Packed 12-bit codes (bitpacked MSB-first, not byte-padded)
// ═══════════════════════════════════════════════════════════════════════

#include "algorithms/ICompressor.hpp"

class LZWCompressor : public ICompressor {
public:
    bool compress(const uint8_t* in, size_t inSize,
                  std::vector<uint8_t>& out) override;

    bool decompress(const uint8_t* in, size_t inSize,
                    std::vector<uint8_t>& out) override;

    std::string name() const override { return "LZW"; }

    static constexpr int CODE_WIDTH   = 12;       // Bits per code
    static constexpr int MAX_DICT     = 1 << 12;  // 4096 entries
    static constexpr int INITIAL_SIZE = 256;       // Single-byte entries
};

#endif // LZW_COMPRESSOR_HPP
