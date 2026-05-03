#ifndef ENTROPY_ANALYZER_HPP
#define ENTROPY_ANALYZER_HPP

// ═══════════════════════════════════════════════════════════════════════
// EntropyAnalyzer — Shannon entropy calculation and algorithm selection
//
// Design Note: What Shannon Entropy Measures Here
// ─────────────────────────────────────────────────────────────────────
// Shannon entropy H quantifies the average "surprise" or "information
// content" per byte in the input. For a byte stream:
//
//   H = -Σ p(x) * log2(p(x))   for each unique byte value x ∈ [0,255]
//
// where p(x) = count(x) / totalBytes.
//
// Range: 0 ≤ H ≤ 8 bits/byte
//   H ≈ 0 : All bytes identical (e.g., all zeros). Maximally redundant.
//   H ≈ 8 : All 256 byte values equally likely. Maximally random.
//            Essentially incompressible.
//
// Why Entropy Predicts Algorithm Suitability:
// ─────────────────────────────────────────────────────────────────────
// HUFFMAN excels when H is low (< 4.0):
//   Low entropy means a few symbols dominate the input. Huffman assigns
//   short bit-codes to frequent symbols, achieving strong compression.
//   Example: English text (H ≈ 3.5–4.5) has skewed letter frequencies.
//
// LZW excels when H is moderate-to-high (≥ 4.0):
//   LZW finds repeated multi-byte SEQUENCES, not just individual byte
//   frequencies. Even when byte distribution is relatively uniform
//   (high entropy), LZW can exploit substring repetition. Binary
//   formats, executables, and structured data often have high byte
//   entropy but strong sequential patterns.
//
// Threshold: H = 4.0 (half of maximum)
//   This is an empirical heuristic, not a theoretical optimum. Real-world
//   testing shows it produces good default behavior for common file types.
//   A production tool would benchmark on a corpus and tune this.
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>
#include <cstddef>
#include <string>

enum class AlgoChoice {
    Huffman,
    LZW
};

class EntropyAnalyzer {
public:
    // Entropy threshold for algorithm selection.
    // Below this → Huffman, at or above → LZW.
    static constexpr double THRESHOLD = 4.0;

    // Calculate Shannon entropy of the given byte buffer.
    // Returns value in [0.0, 8.0] bits per byte.
    // Does NOT open any files — operates purely on the provided buffer.
    static double calculateEntropy(const uint8_t* data, size_t size);

    // Recommend an algorithm based on entropy of the sample.
    // Caller should pass first min(8192, fileSize) bytes.
    static AlgoChoice recommend(const uint8_t* sampleData, size_t sampleSize);

    // Convert AlgoChoice to human-readable string.
    static std::string algoName(AlgoChoice choice);
};

#endif // ENTROPY_ANALYZER_HPP
