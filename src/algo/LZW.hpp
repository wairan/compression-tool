// ═══════════════════════════════════════════════════════════════════════
// LZW.hpp — Lempel-Ziv-Welch compression/decompression
// Stub for Step 2. Will implement:
//   - Dictionary-based compression (12-bit or variable-width codes)
//   - Standard LZW with clear codes and table reset
//   - Decompression with dictionary reconstruction
// ═══════════════════════════════════════════════════════════════════════
#ifndef LZW_HPP
#define LZW_HPP

#include <cstdint>
#include <vector>

namespace algo {

class LZW {
public:
    static std::vector<uint8_t> compress(const uint8_t* data, size_t size);
    static std::vector<uint8_t> decompress(const uint8_t* data, size_t size);
};

} // namespace algo

#endif // LZW_HPP
