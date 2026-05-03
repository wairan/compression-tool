#ifndef HUFFMAN_COMPRESSOR_HPP
#define HUFFMAN_COMPRESSOR_HPP

// ═══════════════════════════════════════════════════════════════════════
// HuffmanCompressor — Huffman Coding compression/decompression
//
// Design Note: Tree Serialization Strategy
// ─────────────────────────────────────────────────────────────────────
// We use a SYMBOL + CODE-LENGTH + CODE-BITS table rather than
// serializing the full tree structure. Tradeoffs:
//
//   Full tree serialization (pre-order with leaf/internal markers):
//     + Simpler reconstruction (just replay the traversal)
//     - Wastes space on internal nodes (up to 255 internal nodes)
//     - Encoding: ~2 bits per node → up to ~64 bytes overhead for
//       internal markers, plus 1 byte per leaf = ~320 bytes worst case
//
//   Symbol + length table (canonical Huffman, used by DEFLATE/gzip):
//     + Most compact header (1 byte symbol + 1 byte length per entry)
//     + Canonical codes can be reconstructed from lengths alone
//     - Requires canonical code assignment algorithm
//
//   Symbol + length + bits (OUR CHOICE):
//     + Preserves the exact tree structure (non-canonical codes OK)
//     + Decompressor needs no code-generation logic — just rebuild tree
//     + Simpler implementation for a university project
//     - Slightly larger header than canonical-length-only approach
//       (extra ceil(codeLen/8) bytes per symbol)
//     - Worst case for 256 symbols with ~20-bit codes: ~1.2 KB header
//       (acceptable for files worth compressing)
// ═══════════════════════════════════════════════════════════════════════

#include "algorithms/ICompressor.hpp"
#include <array>
#include <memory>

class HuffmanCompressor : public ICompressor {
public:
    bool compress(const uint8_t* in, size_t inSize,
                  std::vector<uint8_t>& out) override;

    bool decompress(const uint8_t* in, size_t inSize,
                    std::vector<uint8_t>& out) override;

    std::string name() const override { return "Huffman"; }

    // ── Internal types (public for testability) ─────────────────────

    // Huffman tree node
    struct Node {
        uint8_t symbol;
        uint64_t freq;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;

        // Leaf constructor
        Node(uint8_t s, uint64_t f)
            : symbol(s), freq(f), left(nullptr), right(nullptr) {}

        // Internal node constructor
        Node(uint64_t f, std::unique_ptr<Node> l, std::unique_ptr<Node> r)
            : symbol(0), freq(f), left(std::move(l)), right(std::move(r)) {}

        bool isLeaf() const { return !left && !right; }
    };

    // Code entry: bits and length for one symbol
    struct CodeEntry {
        uint32_t code   = 0;  // The code bits (MSB-aligned within 'length' bits)
        uint8_t  length = 0;  // Number of valid bits in 'code'
    };

private:
    // Build byte frequency histogram from input
    static std::array<uint64_t, 256> buildFrequencyTable(
        const uint8_t* data, size_t size);

    // Build Huffman tree from frequency table using min-heap
    static std::unique_ptr<Node> buildTree(
        const std::array<uint64_t, 256>& freq);

    // DFS to generate bit-codes from tree
    static void generateCodes(const Node* node, uint32_t code, uint8_t depth,
                               std::array<CodeEntry, 256>& codes);

    // Rebuild tree from code table (for decompression)
    static std::unique_ptr<Node> rebuildTree(
        const std::array<CodeEntry, 256>& codes);
};

#endif // HUFFMAN_COMPRESSOR_HPP
