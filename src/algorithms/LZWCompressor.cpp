#include "algorithms/LZWCompressor.hpp"

#include <unordered_map>  // Dictionary for compression (string → code)
#include <cstring>        // memcmp for magic byte checks
#include <iostream>       // std::cerr for diagnostics

// No file I/O headers (fstream, cstdio) — all I/O through buffer interface.

// ─── Utility: big-endian read/write ─────────────────────────────────
static void writeBE16(std::vector<uint8_t>& out, uint16_t val) {
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(val & 0xFF));
}

static void writeBE64(std::vector<uint8_t>& out, uint64_t val) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
}

static uint16_t readBE16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint64_t readBE64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

// ─────────────────────────────────────────────────────────────────────
// BitWriter12: packs 12-bit codes into bytes (MSB-first, bitpacked)
// ─────────────────────────────────────────────────────────────────────
class BitWriter12 {
public:
    explicit BitWriter12(std::vector<uint8_t>& output) : out_(output) {}

    void writeCode(uint16_t code, int width) {
        // Write 'width' bits of 'code', MSB first
        for (int i = width - 1; i >= 0; --i) {
            uint8_t bit = (code >> i) & 1;
            if (bitCount_ == 0) out_.push_back(0);
            if (bit) out_.back() |= (1 << (7 - bitCount_));
            bitCount_++;
            if (bitCount_ == 8) bitCount_ = 0;
        }
    }

    void flush() { bitCount_ = 0; }

private:
    std::vector<uint8_t>& out_;
    int bitCount_ = 0;
};

// ─────────────────────────────────────────────────────────────────────
// BitReader12: reads N-bit codes from a byte array (MSB-first)
// ─────────────────────────────────────────────────────────────────────
class BitReader12 {
public:
    BitReader12(const uint8_t* data, size_t size)
        : data_(data), size_(size) {}

    // Read a code of 'width' bits. Returns -1 if past end.
    int readCode(int width) {
        uint32_t val = 0;
        for (int i = 0; i < width; ++i) {
            if (bytePos_ >= size_) return -1;
            int bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;
            val = (val << 1) | bit;
            bitPos_++;
            if (bitPos_ == 8) { bitPos_ = 0; bytePos_++; }
        }
        return static_cast<int>(val);
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

bool LZWCompressor::compress(
    const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
    out.clear();

    // ── Write header ────────────────────────────────────────────────
    // [0..3] Magic
    out.push_back('L'); out.push_back('Z');
    out.push_back('W'); out.push_back('_');
    // [4..5] Max code width = 12 bits
    writeBE16(out, static_cast<uint16_t>(CODE_WIDTH));
    // [6..13] Original uncompressed size
    writeBE64(out, static_cast<uint64_t>(inSize));

    if (inSize == 0) return true;  // Header-only for empty input

    // ── Initialize dictionary with all 256 single-byte strings ──────
    // Key: the string (as a std::string of bytes), Value: code number.
    // We use std::string as a byte-string here (not text) because
    // std::unordered_map<string> gives us O(1) average lookup.
    std::unordered_map<std::string, uint16_t> dict;
    dict.reserve(MAX_DICT);
    for (int i = 0; i < INITIAL_SIZE; ++i) {
        dict[std::string(1, static_cast<char>(i))] = static_cast<uint16_t>(i);
    }
    uint16_t nextCode = INITIAL_SIZE;  // Next code to assign: 256

    // ── LZW compression loop ────────────────────────────────────────
    // Standard algorithm:
    //   w = first byte
    //   for each subsequent byte c:
    //     if w+c in dictionary: w = w+c (extend the match)
    //     else: output code(w), add w+c to dictionary, w = c
    //   output code(w) for the final string
    BitWriter12 bw(out);
    std::string w(1, static_cast<char>(in[0]));

    for (size_t i = 1; i < inSize; ++i) {
        std::string wc = w + static_cast<char>(in[i]);

        if (dict.count(wc)) {
            // String w+c is already in dictionary — extend the match
            w = wc;
        } else {
            // Output the code for the current match 'w'
            bw.writeCode(dict[w], CODE_WIDTH);

            // Add w+c to dictionary if space remains
            // When dictionary is full (4096 entries for 12-bit codes),
            // we stop adding. The existing dictionary continues to work
            // for matching — we just can't learn new patterns.
            if (nextCode < MAX_DICT) {
                dict[wc] = nextCode++;
            }

            // Reset: start new match from character 'c'
            w = std::string(1, static_cast<char>(in[i]));
        }
    }

    // Output the code for the last match
    bw.writeCode(dict[w], CODE_WIDTH);
    bw.flush();

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//                       DECOMPRESSION
// ═══════════════════════════════════════════════════════════════════════

bool LZWCompressor::decompress(
    const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
    out.clear();

    // ── Validate and read header ────────────────────────────────────
    if (inSize < 14) {
        std::cerr << "LZW: Input too small for header\n";
        return false;
    }
    if (memcmp(in, "LZW_", 4) != 0) {
        std::cerr << "LZW: Invalid magic bytes\n";
        return false;
    }

    uint16_t codeWidth = readBE16(in + 4);
    uint64_t originalSize = readBE64(in + 6);

    if (codeWidth != CODE_WIDTH) {
        std::cerr << "LZW: Unsupported code width " << codeWidth << "\n";
        return false;
    }

    if (originalSize == 0) return true;

    // ── Initialize dictionary with 256 single-byte entries ──────────
    // For decompression, the dictionary maps code → string.
    // We use a vector of strings for O(1) indexed access.
    std::vector<std::string> dict;
    dict.reserve(MAX_DICT);
    for (int i = 0; i < INITIAL_SIZE; ++i) {
        dict.push_back(std::string(1, static_cast<char>(i)));
    }

    // ── LZW decompression loop ──────────────────────────────────────
    BitReader12 br(in + 14, inSize - 14);
    out.reserve(static_cast<size_t>(originalSize));

    // Read the first code
    int firstCode = br.readCode(CODE_WIDTH);
    if (firstCode < 0 || firstCode >= INITIAL_SIZE) {
        std::cerr << "LZW: Invalid first code\n";
        return false;
    }

    std::string w = dict[firstCode];
    // Output the first string
    for (char c : w) out.push_back(static_cast<uint8_t>(c));

    // Decode remaining codes
    while (out.size() < static_cast<size_t>(originalSize)) {
        int code = br.readCode(CODE_WIDTH);
        if (code < 0) {
            std::cerr << "LZW: Unexpected end of code stream\n";
            return false;
        }

        std::string entry;

        if (static_cast<size_t>(code) < dict.size()) {
            // Normal case: code exists in dictionary
            entry = dict[code];
        }
        else if (static_cast<size_t>(code) == dict.size()) {
            // ─────────────────────────────────────────────────────────
            // CLASSIC LZW EDGE CASE: code == next_code
            //
            // This happens when the encoder adds a new dictionary entry
            // and then immediately uses it in the very next output code.
            //
            // Example: input "ABABAB..."
            //   Encoder sees "AB" (outputs code for 'A', adds "AB"=256)
            //   Then sees "BA" (outputs code for 'B', adds "BA"=257)
            //   Then sees "AB" again — but now "ABA" is being built:
            //     outputs 256 ("AB"), adds "ABA"=258
            //   Then sees "ABA" — outputs 258... but the decoder hasn't
            //     added 258 yet when it receives it!
            //
            // The fix: if code == dict.size() (the next code that WOULD
            // be assigned), the string must be: w + w[0]
            // (the previous string plus its first character).
            // This is provably correct because the only way the encoder
            // emits next_code is when the new entry's string starts with
            // the same character the previous string started with.
            // ─────────────────────────────────────────────────────────
            entry = w + w[0];
        }
        else {
            std::cerr << "LZW: Code " << code << " out of range "
                      << "(dict size: " << dict.size() << ")\n";
            return false;
        }

        // Output the decoded string
        for (char c : entry) out.push_back(static_cast<uint8_t>(c));

        // Add new dictionary entry: previous string + first char of current
        if (dict.size() < static_cast<size_t>(MAX_DICT)) {
            dict.push_back(w + entry[0]);
        }

        w = entry;
    }

    // Trim to exact original size (last code may produce extra bytes)
    out.resize(static_cast<size_t>(originalSize));

    return true;
}
