// ═══════════════════════════════════════════════════════════════════════
// main.cpp — Orchestration entry point
//
// This file contains ZERO algorithm logic. It is pure dispatch:
//   1. Parse CLI arguments
//   2. Open input file via MmapReader
//   3. Decide algorithm (auto-select or forced)
//   4. Dispatch to compressor or decompressor
//   5. Write output via FileWriter
//   6. Print stats if --verbose
//
// All business logic lives in:
//   - algorithms/    (Huffman, LZW)
//   - threading/     (ThreadPool, ParallelCompressor)
//   - analysis/      (EntropyAnalyzer)
//   - formats/       (CompressedFile)
// ═══════════════════════════════════════════════════════════════════════

#include "cli/CLI.hpp"
#include "io/MmapReader.hpp"
#include "io/FileWriter.hpp"
#include "analysis/EntropyAnalyzer.hpp"
#include "algorithms/ICompressor.hpp"
#include "algorithms/HuffmanCompressor.hpp"
#include "algorithms/LZWCompressor.hpp"
#include "formats/CompressedFile.hpp"
#include "threading/ParallelCompressor.hpp"
#include "threading/ParallelHeader.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <memory>
#include <algorithm>  // std::min
#include <cstring>    // memcmp

// Maximum sample size for entropy analysis (first 8 KB of the file)
static constexpr size_t ENTROPY_SAMPLE_SIZE = 8192;

// ─────────────────────────────────────────────────────────────────────
// Compression flow
// ─────────────────────────────────────────────────────────────────────
static int doCompress(const CLIArgs& args) {
    // ── Open input ──────────────────────────────────────────────────
    MmapReader reader(args.inputPath);
    const uint8_t* inputData = reader.data();
    size_t inputSize = reader.size();

    if (inputSize == 0) {
        std::cerr << "Error: Input file is empty\n";
        return 1;
    }

    // ── Algorithm selection ─────────────────────────────────────────
    // Determine which algorithm to use: explicit choice or auto-select
    uint8_t algoId = 0;
    std::unique_ptr<ICompressor> algo;
    double entropy = 0.0;
    std::string choiceReason;

    // Always compute entropy for --verbose, even if algo is forced
    size_t sampleSize = std::min(inputSize, ENTROPY_SAMPLE_SIZE);
    entropy = EntropyAnalyzer::calculateEntropy(inputData, sampleSize);

    switch (args.algo) {
        case AlgoMode::Auto: {
            AlgoChoice choice = EntropyAnalyzer::recommend(
                inputData, sampleSize);
            if (choice == AlgoChoice::Huffman) {
                algo = std::make_unique<HuffmanCompressor>();
                algoId = CompressedFile::ALGO_HUFFMAN;
                choiceReason = "auto (entropy=" +
                    std::to_string(entropy).substr(0, 4) +
                    " < " + std::to_string(EntropyAnalyzer::THRESHOLD).substr(0, 3) + ")";
            } else {
                algo = std::make_unique<LZWCompressor>();
                algoId = CompressedFile::ALGO_LZW;
                choiceReason = "auto (entropy=" +
                    std::to_string(entropy).substr(0, 4) +
                    " >= " + std::to_string(EntropyAnalyzer::THRESHOLD).substr(0, 3) + ")";
            }
            break;
        }
        case AlgoMode::Huffman:
            algo = std::make_unique<HuffmanCompressor>();
            algoId = CompressedFile::ALGO_HUFFMAN;
            choiceReason = "forced";
            break;
        case AlgoMode::LZW:
            algo = std::make_unique<LZWCompressor>();
            algoId = CompressedFile::ALGO_LZW;
            choiceReason = "forced";
            break;
    }

    // ── Compress ────────────────────────────────────────────────────
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> outputData;
    bool success = false;

    if (args.numThreads > 1) {
        // Parallel compression: produces PRLZ format (includes algo ID)
        success = ParallelCompressor::compress(
            *algo, algoId,
            inputData, inputSize,
            outputData,
            args.chunkSize,
            args.numThreads
        );
    } else {
        // Single-threaded: compress then wrap in CompressedFile envelope
        std::vector<uint8_t> rawCompressed;
        success = algo->compress(inputData, inputSize, rawCompressed);
        if (success) {
            outputData = CompressedFile::wrap(algoId, rawCompressed);
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!success) {
        std::cerr << "Error: Compression failed\n";
        return 1;
    }

    // ── Write output ────────────────────────────────────────────────
    FileWriter writer(args.outputPath);
    if (!writer.write(outputData.data(), outputData.size())) {
        std::cerr << "Error: Failed to write output file\n";
        return 1;
    }
    writer.flush();

    // ── Verbose stats ───────────────────────────────────────────────
    if (args.verbose) {
        double ratio = static_cast<double>(inputSize) / outputData.size();
        std::cout << std::fixed << std::setprecision(2)
            << "[INFO] Input size:        " << inputSize << " bytes\n"
            << "[INFO] Algorithm:         " << algo->name()
            << " (" << choiceReason << ")\n"
            << "[INFO] Entropy (sample):  " << entropy << " bits/byte\n"
            << "[INFO] Output size:       " << outputData.size() << " bytes\n"
            << "[INFO] Ratio:             " << ratio << ":1\n"
            << "[INFO] Threads used:      " << args.numThreads << "\n"
            << "[INFO] Wall time:         " << wallMs << " ms\n";
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────
// Decompression flow
//
// Auto-detects the format from magic bytes in the file header:
//   "PRLZ" → Parallel format → ParallelCompressor::decompress
//   "CF"   → Single-thread format → CompressedFile::unwrap + algo->decompress
// The user does NOT need to specify --algo for decompression.
// ─────────────────────────────────────────────────────────────────────
static int doDecompress(const CLIArgs& args) {
    // ── Open input ──────────────────────────────────────────────────
    MmapReader reader(args.inputPath);
    const uint8_t* inputData = reader.data();
    size_t inputSize = reader.size();

    if (inputSize < 4) {
        std::cerr << "Error: Input file too small to be a compressed file\n";
        return 1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> outputData;
    std::string algoName;
    bool success = false;

    // ── Detect format from magic bytes ──────────────────────────────
    if (memcmp(inputData, "PRLZ", 4) == 0) {
        // Parallel format: the PRLZ header contains the algorithm ID
        ParallelHeader hdr{};
        if (!hdr.readFrom(inputData, inputSize)) {
            std::cerr << "Error: Invalid parallel header\n";
            return 1;
        }

        // Instantiate the correct algorithm from the stored ID
        std::unique_ptr<ICompressor> algo;
        if (hdr.algoId == CompressedFile::ALGO_HUFFMAN) {
            algo = std::make_unique<HuffmanCompressor>();
        } else if (hdr.algoId == CompressedFile::ALGO_LZW) {
            algo = std::make_unique<LZWCompressor>();
        } else {
            std::cerr << "Error: Unknown algorithm ID 0x"
                      << std::hex << static_cast<int>(hdr.algoId) << " in PRLZ header\n";
            return 1;
        }
        algoName = algo->name();

        success = ParallelCompressor::decompress(
            *algo, inputData, inputSize, outputData, args.numThreads);

    } else if (inputSize >= 2 && inputData[0] == 'C' && inputData[1] == 'F') {
        // CompressedFile format (single-threaded)
        try {
            auto result = CompressedFile::unwrap(inputData, inputSize);

            std::unique_ptr<ICompressor> algo;
            if (result.algoId == CompressedFile::ALGO_HUFFMAN) {
                algo = std::make_unique<HuffmanCompressor>();
            } else if (result.algoId == CompressedFile::ALGO_LZW) {
                algo = std::make_unique<LZWCompressor>();
            } else {
                std::cerr << "Error: Unknown algorithm ID in CF header\n";
                return 1;
            }
            algoName = algo->name();

            success = algo->decompress(
                result.payload, result.payloadSize, outputData);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
    } else {
        std::cerr << "Error: Unrecognized file format. "
                  << "Expected 'PRLZ' or 'CF' magic bytes, got '"
                  << static_cast<char>(inputData[0])
                  << static_cast<char>(inputData[1])
                  << static_cast<char>(inputData[2])
                  << static_cast<char>(inputData[3]) << "'\n";
        return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double wallMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!success) {
        std::cerr << "Error: Decompression failed\n";
        return 1;
    }

    // ── Write output ────────────────────────────────────────────────
    FileWriter writer(args.outputPath);
    if (!writer.write(outputData.data(), outputData.size())) {
        std::cerr << "Error: Failed to write output file\n";
        return 1;
    }
    writer.flush();

    // ── Verbose stats ───────────────────────────────────────────────
    if (args.verbose) {
        double ratio = (outputData.size() > 0)
            ? static_cast<double>(outputData.size()) / inputSize
            : 0.0;
        std::cout << std::fixed << std::setprecision(2)
            << "[INFO] Input size:        " << inputSize << " bytes (compressed)\n"
            << "[INFO] Algorithm:         " << algoName << " (from header)\n"
            << "[INFO] Output size:       " << outputData.size() << " bytes (decompressed)\n"
            << "[INFO] Expansion:         " << ratio << ":1\n"
            << "[INFO] Threads used:      " << args.numThreads << "\n"
            << "[INFO] Wall time:         " << wallMs << " ms\n";
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────
// main: parse CLI, dispatch to compress or decompress
// ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    CLIArgs args;
    if (!CLI::parse(argc, argv, args)) {
        return 1;
    }

    try {
        if (args.decompress) {
            return doDecompress(args);
        } else {
            return doCompress(args);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
