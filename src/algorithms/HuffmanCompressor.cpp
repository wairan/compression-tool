#include "algorithms/HuffmanCompressor.hpp"

#include <queue>       // std::priority_queue for min-heap tree construction
#include <functional>  // std::greater (not used directly, custom comparator)
#include <cstring>     // memcmp for magic byte checks
#include <stdexcept>   // std::runtime_error
#include <iostream>    // std::cerr for diagnostics

// No file I/O headers (fstream, cstdio) — all I/O goes through the
// buffer interface defined in ICompressor.

// ═══════════════════════════════════════════════════════════════════════
// Header format (byte offsets documented):
//   [0..3]   4 bytes: magic "HUFF"
//   [4..5]   2 bytes: number of unique symbols N (uint16_t, big-endian)
//   [6..]    Per symbol (repeated N times):
//              1 byte:  symbol value
//              1 byte:  code length L (in bits)
//              ceil(L/8) bytes: code bits (MSB-first, zero-padded)
//   [after symbols] 8 bytes: original uncompressed size (uint64_t, big-endian)
//   [remainder]     encoded bitstream
// ═══════════════════════════════════════════════════════════════════════

// ─── Utility: write a big-endian uint16_t ────────────────────────────
static void writeBE16(std::vector<uint8_t>& out, uint16_t val) {
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(val & 0xFF));
}

// ─── Utility: write a big-endian uint64_t ────────────────────────────
static void writeBE64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

// ─── Utility: read a big-endian uint16_t ─────────────────────────────
static uint16_t readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// ─── Utility: read a big-endian uint64_t ─────────────────────────────
static uint64_t readBE64(const uint8_t* p) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val = (val << 8) | p[i];
    }
    return val;
}

// ─────────────────────────────────────────────────────────────────────
// BitWriter: packs individual bits into a byte vector (MSB-first)
// Used for writing the encoded bitstream.
// ─────────────────────────────────────────────────────────────────────
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& output) : out_(output) {}

    void writeBits(uint32_t value, int numBits) {
        // Write bits from MSB to LSB of 'value'
        for (int i = numBits - 1; i >= 0; --i) {
            uint8_t bit = (value >> i) & 1;
            if (bitCount_ == 0) {
                out_.push_back(0);
            }
            if (bit) {
                out_.back() |= (1 << (7 - bitCount_));
            }
            bitCount_++;
            if (bitCount_ == 8) {
                bitCount_ = 0;
            }
        }
    }

    // Flush: any partial byte is already zero-padded from push_back(0)
    void flush() { bitCount_ = 0; }

private:
    std::vector<uint8_t>& out_;
    int bitCount_ = 0;
};

// ─────────────────────────────────────────────────────────────────────
// BitReader: reads individual bits from a byte array (MSB-first)
// Used for decoding the compressed bitstream.
// ─────────────────────────────────────────────────────────────────────
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}

    // Returns 0 or 1, or -1 if past end
    int readBit() {
        if (bytePos_ >= size_) return -1;
        int bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;
        bitPos_++;
        if (bitPos_ == 8) {
            bitPos_ = 0;
            bytePos_++;
        }
        return bit;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t bytePos_ = 0;
    int    bitPos_   = 0;
};

// ═══════════════════════════════════════════════════════════════════════
//                        COMPRESSION
// ═══════════════════════════════════════════════════════════════════════

std::array<uint64_t, 256> HuffmanCompressor::buildFrequencyTable(
    const uint8_t* data, size_t size)
{
    // Scan every byte and count occurrences.
    // O(n) time, O(1) space (fixed 256-entry array).
    std::array<uint64_t, 256> freq{};
    for (size_t i = 0; i < size; ++i) {
        freq[data[i]]++;
    }
    return freq;
}

std::unique_ptr<HuffmanCompressor::Node> HuffmanCompressor::buildTree(
    const std::array<uint64_t, 256>& freq)
{
    // Min-heap priority queue: smallest frequency = highest priority.
    // This is the textbook Huffman tree construction algorithm:
    //   1. Insert all symbols with non-zero frequency as leaf nodes
    //   2. Repeatedly extract the two smallest, merge into an internal node
    //   3. The last remaining node is the root
    auto cmp = [](const std::unique_ptr<Node>& a,
                  const std::unique_ptr<Node>& b) {
        return a->freq > b->freq;  // Min-heap: smaller freq = higher priority
    };

    std::priority_queue<
        std::unique_ptr<Node>,
        std::vector<std::unique_ptr<Node>>,
        decltype(cmp)
    > pq(cmp);

    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            pq.push(std::make_unique<Node>(
                static_cast<uint8_t>(i), freq[i]));
        }
    }

    // Edge case: no symbols (empty input — should be caught earlier)
    if (pq.empty()) {
        return nullptr;
    }

    // Edge case: single unique symbol.
    // A Huffman tree needs at least two nodes to produce a binary code.
    // With one symbol, we add a dummy node so the symbol gets code "0".
    if (pq.size() == 1) {
        auto only = std::move(const_cast<std::unique_ptr<Node>&>(pq.top()));
        pq.pop();
        auto dummy = std::make_unique<Node>(0, 0);
        auto merged = std::make_unique<Node>(
            only->freq,
            std::move(only),
            std::move(dummy)
        );
        return merged;
    }

    // Standard Huffman merge loop
    while (pq.size() > 1) {
        auto left = std::move(const_cast<std::unique_ptr<Node>&>(pq.top()));
        pq.pop();
        auto right = std::move(const_cast<std::unique_ptr<Node>&>(pq.top()));
        pq.pop();

        auto parent = std::make_unique<Node>(
            left->freq + right->freq,
            std::move(left),
            std::move(right)
        );
        pq.push(std::move(parent));
    }

    auto root = std::move(const_cast<std::unique_ptr<Node>&>(pq.top()));
    pq.pop();
    return root;
}

void HuffmanCompressor::generateCodes(
    const Node* node, uint32_t code, uint8_t depth,
    std::array<CodeEntry, 256>& codes)
{
    if (!node) return;

    if (node->isLeaf()) {
        // At a leaf: record the code for this symbol.
        // 'code' contains the bit path from root to this leaf,
        // and 'depth' is how many bits are valid.
        codes[node->symbol] = {code, depth};
        return;
    }

    // Traverse left (append bit 0) and right (append bit 1)
    generateCodes(node->left.get(),  (code << 1) | 0, depth + 1, codes);
    generateCodes(node->right.get(), (code << 1) | 1, depth + 1, codes);
}

std::unique_ptr<HuffmanCompressor::Node> HuffmanCompressor::rebuildTree(
    const std::array<CodeEntry, 256>& codes)
{
    // Reconstruct the Huffman tree by inserting each symbol's code path.
    // For each symbol, walk the code bits from MSB to LSB, creating
    // internal nodes as needed, and place the symbol at the leaf.
    auto root = std::make_unique<Node>(0, 0);

    for (int sym = 0; sym < 256; ++sym) {
        if (codes[sym].length == 0) continue;

        Node* current = root.get();
        uint32_t c = codes[sym].code;
        uint8_t  len = codes[sym].length;

        for (int bit = len - 1; bit >= 0; --bit) {
            int b = (c >> bit) & 1;
            if (b == 0) {
                if (!current->left) {
                    current->left = std::make_unique<Node>(0, 0);
                }
                current = current->left.get();
            } else {
                if (!current->right) {
                    current->right = std::make_unique<Node>(0, 0);
                }
                current = current->right.get();
            }
        }
        // Mark this node as a leaf with the correct symbol
        current->symbol = static_cast<uint8_t>(sym);
        // Leaf nodes have no children (left/right remain nullptr)
    }

    return root;
}

bool HuffmanCompressor::compress(
    const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
    out.clear();

    // ── Handle empty input ──────────────────────────────────────────
    if (inSize == 0) {
        out.push_back('H'); out.push_back('U');
        out.push_back('F'); out.push_back('F');
        writeBE16(out, 0);      // 0 unique symbols
        writeBE64(out, 0);      // 0 original size
        return true;
    }

    // ── Step 1: Build frequency table ───────────────────────────────
    auto freq = buildFrequencyTable(in, inSize);

    // ── Step 2: Build Huffman tree from min-heap ────────────────────
    auto tree = buildTree(freq);
    if (!tree) {
        std::cerr << "Huffman: Failed to build tree\n";
        return false;
    }

    // ── Step 3: Generate canonical bit-codes per symbol ─────────────
    std::array<CodeEntry, 256> codes{};
    generateCodes(tree.get(), 0, 0, codes);

    // ── Step 4: Serialize header ────────────────────────────────────
    // [0..3] Magic "HUFF"
    out.push_back('H'); out.push_back('U');
    out.push_back('F'); out.push_back('F');

    // [4..5] Number of unique symbols (big-endian uint16_t)
    uint16_t numSymbols = 0;
    for (auto& c : codes) {
        if (c.length > 0) numSymbols++;
    }
    writeBE16(out, numSymbols);

    // [6..] Per-symbol entries
    for (int i = 0; i < 256; ++i) {
        if (codes[i].length == 0) continue;

        out.push_back(static_cast<uint8_t>(i));   // Symbol value
        out.push_back(codes[i].length);            // Code length in bits

        // Code bits: ceil(length/8) bytes, MSB-first, zero-padded right
        int numBytes = (codes[i].length + 7) / 8;
        int totalBits = numBytes * 8;
        // Left-align the code in 'totalBits' space
        uint32_t shifted = codes[i].code << (totalBits - codes[i].length);
        for (int b = numBytes - 1; b >= 0; --b) {
            out.push_back(static_cast<uint8_t>((shifted >> (b * 8)) & 0xFF));
        }
    }

    // [after symbols] Original uncompressed size (big-endian uint64_t)
    writeBE64(out, static_cast<uint64_t>(inSize));

    // ── Step 5: Encode the bitstream ────────────────────────────────
    BitWriter bw(out);
    for (size_t i = 0; i < inSize; ++i) {
        const auto& entry = codes[in[i]];
        bw.writeBits(entry.code, entry.length);
    }
    bw.flush();

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//                       DECOMPRESSION
// ═══════════════════════════════════════════════════════════════════════

bool HuffmanCompressor::decompress(
    const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
    out.clear();

    // ── Validate minimum header size (magic + numSymbols + origSize) ─
    if (inSize < 14) {
        std::cerr << "Huffman: Input too small for header\n";
        return false;
    }

    // ── Read and validate magic bytes ───────────────────────────────
    if (memcmp(in, "HUFF", 4) != 0) {
        std::cerr << "Huffman: Invalid magic bytes\n";
        return false;
    }

    size_t pos = 4;

    // ── Read number of unique symbols ───────────────────────────────
    uint16_t numSymbols = readBE16(in + pos);
    pos += 2;

    // ── Read per-symbol code entries ────────────────────────────────
    std::array<CodeEntry, 256> codes{};
    for (uint16_t s = 0; s < numSymbols; ++s) {
        if (pos + 2 > inSize) {
            std::cerr << "Huffman: Truncated symbol table\n";
            return false;
        }

        uint8_t symbol    = in[pos++];
        uint8_t codeLen   = in[pos++];
        int     numBytes  = (codeLen + 7) / 8;

        if (pos + static_cast<size_t>(numBytes) > inSize) {
            std::cerr << "Huffman: Truncated code bits\n";
            return false;
        }

        // Read code bits (big-endian byte order)
        uint32_t codeBits = 0;
        for (int b = numBytes - 1; b >= 0; --b) {
            codeBits |= static_cast<uint32_t>(in[pos++]) << (b * 8);
        }
        // Right-shift to recover the actual code value
        int totalBits = numBytes * 8;
        codeBits >>= (totalBits - codeLen);

        codes[symbol] = {codeBits, codeLen};
    }

    // ── Read original uncompressed size ─────────────────────────────
    if (pos + 8 > inSize) {
        std::cerr << "Huffman: Truncated original size field\n";
        return false;
    }
    uint64_t originalSize = readBE64(in + pos);
    pos += 8;

    // ── Handle empty original file ──────────────────────────────────
    if (originalSize == 0) {
        return true;
    }

    // ── Rebuild the Huffman tree from code table ────────────────────
    // The decompressor reconstructs the tree from the header alone —
    // no global state or external data needed.
    auto tree = rebuildTree(codes);
    if (!tree) {
        std::cerr << "Huffman: Failed to rebuild tree\n";
        return false;
    }

    // ── Decode the bitstream ────────────────────────────────────────
    out.reserve(static_cast<size_t>(originalSize));
    BitReader br(in + pos, inSize - pos);

    const Node* current = tree.get();
    while (out.size() < static_cast<size_t>(originalSize)) {
        int bit = br.readBit();
        if (bit < 0) {
            std::cerr << "Huffman: Unexpected end of bitstream\n";
            return false;
        }

        // Traverse tree: 0 = left, 1 = right
        current = (bit == 0) ? current->left.get() : current->right.get();
        if (!current) {
            std::cerr << "Huffman: Invalid code in bitstream\n";
            return false;
        }

        if (current->isLeaf()) {
            out.push_back(current->symbol);
            current = tree.get();  // Reset to root for next symbol
        }
    }

    return true;
}
