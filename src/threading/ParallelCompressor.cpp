#include "threading/ParallelCompressor.hpp"
#include "threading/ThreadPool.hpp"
#include "threading/ParallelHeader.hpp"

#include <iostream>    // std::cerr
#include <algorithm>   // std::min
#include <cstring>     // memcmp

// ═══════════════════════════════════════════════════════════════════════
//                     PARALLEL COMPRESSION
// ═══════════════════════════════════════════════════════════════════════

bool ParallelCompressor::compress(
    ICompressor& algo, uint8_t algoId,
    const uint8_t* in, size_t inSize,
    std::vector<uint8_t>& out,
    size_t chunkSize, size_t numThreads)
{
    out.clear();

    if (inSize == 0) {
        // Write a header with zero chunks
        ParallelHeader hdr{};
        hdr.algoId = algoId;
        hdr.chunkCount = 0;
        hdr.chunkSize = static_cast<uint32_t>(chunkSize);
        hdr.totalOriginalSize = 0;
        hdr.writeTo(out);
        return true;
    }

    // ── Step 1: Calculate chunk boundaries ──────────────────────────
    // Split input into ceil(inSize / chunkSize) chunks.
    // The last chunk may be smaller than chunkSize.
    uint32_t numChunks = static_cast<uint32_t>(
        (inSize + chunkSize - 1) / chunkSize);

    // ── Step 2: Allocate per-chunk output buffers ───────────────────
    // Each chunk gets its own vector — NO shared mutable state.
    // This is the key to thread safety: workers never touch each
    // other's memory. The vector of vectors is sized upfront and
    // each slot is written by exactly one thread.
    std::vector<std::vector<uint8_t>> chunkResults(numChunks);
    std::vector<bool> chunkSuccess(numChunks, false);

    // ── Step 3: Create thread pool and enqueue tasks ────────────────
    {
        // Thread pool lifetime is scoped — destructor joins all threads,
        // ensuring all futures are resolved before we proceed to assembly.
        ThreadPool pool(numThreads);
        std::vector<std::future<void>> futures;
        futures.reserve(numChunks);

        for (uint32_t i = 0; i < numChunks; ++i) {
            // Calculate this chunk's byte range in the input
            size_t offset = static_cast<size_t>(i) * chunkSize;
            size_t thisChunkSize = std::min(chunkSize, inSize - offset);

            // Capture by value: i (chunk index), offset, thisChunkSize
            // Capture by reference: algo (shared, but compress() is
            //   logically const / thread-safe since each call uses only
            //   its own local state), chunkResults, chunkSuccess, in
            //
            // SAFETY: 'in' is read-only and never modified.
            // chunkResults[i] is written by exactly one thread (thread i).
            // No two threads write to the same index.
            futures.push_back(pool.enqueue(
                [&algo, in, offset, thisChunkSize, i,
                 &chunkResults, &chunkSuccess]()
                {
                    chunkSuccess[i] = algo.compress(
                        in + offset, thisChunkSize, chunkResults[i]);
                }
            ));
        }

        // Wait for all tasks to complete.
        // future::get() will re-throw any exception from the worker.
        for (auto& f : futures) {
            f.get();
        }
    } // ThreadPool destructor: shutdown() + join all workers

    // ── Step 4: Check all chunks succeeded ──────────────────────────
    for (uint32_t i = 0; i < numChunks; ++i) {
        if (!chunkSuccess[i]) {
            std::cerr << "ParallelCompressor: Chunk " << i
                      << " failed to compress\n";
            return false;
        }
    }

    // ── Step 5: Assemble output (single-threaded, in order) ─────────
    // This is where ordering is enforced. During parallel execution,
    // chunks may complete in any order (e.g., chunk 3 before chunk 0).
    // But we stored results in chunkResults[i] by index, so iterating
    // 0..N-1 gives us original order regardless of completion order.

    // Write the parallel header
    ParallelHeader hdr{};
    hdr.algoId = algoId;
    hdr.chunkCount = numChunks;
    hdr.chunkSize = static_cast<uint32_t>(chunkSize);
    hdr.totalOriginalSize = static_cast<uint64_t>(inSize);
    hdr.writeTo(out);

    // Write each chunk: [compressed_size:uint32_t LE][compressed_data]
    for (uint32_t i = 0; i < numChunks; ++i) {
        uint32_t compSize = static_cast<uint32_t>(chunkResults[i].size());
        ParallelHeader::writeLE32(out, compSize);
        out.insert(out.end(),
                   chunkResults[i].begin(), chunkResults[i].end());
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//                    PARALLEL DECOMPRESSION
// ═══════════════════════════════════════════════════════════════════════

bool ParallelCompressor::decompress(
    ICompressor& algo,
    const uint8_t* in, size_t inSize,
    std::vector<uint8_t>& out,
    size_t numThreads)
{
    out.clear();

    // ── Read and validate the parallel header ───────────────────────
    ParallelHeader hdr{};
    if (!hdr.readFrom(in, inSize)) {
        std::cerr << "ParallelCompressor: Invalid parallel header\n";
        return false;
    }

    if (hdr.totalOriginalSize == 0) {
        return true;  // Empty file
    }

    // ── Parse chunk table: read compressed sizes and locate data ────
    // We need to know each chunk's offset and size in the input buffer
    // before we can dispatch parallel decompression tasks.
    struct ChunkInfo {
        size_t   offset;          // Offset into 'in' where compressed data starts
        uint32_t compressedSize;  // Size of compressed data
    };

    std::vector<ChunkInfo> chunks(hdr.chunkCount);
    size_t pos = ParallelHeader::HEADER_SIZE;

    for (uint32_t i = 0; i < hdr.chunkCount; ++i) {
        if (pos + 4 > inSize) {
            std::cerr << "ParallelCompressor: Truncated chunk table at chunk "
                      << i << "\n";
            return false;
        }

        uint32_t compSize = ParallelHeader::readLE32(in + pos);
        pos += 4;

        if (pos + compSize > inSize) {
            std::cerr << "ParallelCompressor: Truncated chunk data at chunk "
                      << i << "\n";
            return false;
        }

        chunks[i] = {pos, compSize};
        pos += compSize;
    }

    // ── Decompress chunks in parallel ───────────────────────────────
    std::vector<std::vector<uint8_t>> chunkResults(hdr.chunkCount);
    std::vector<bool> chunkSuccess(hdr.chunkCount, false);

    {
        ThreadPool pool(numThreads);
        std::vector<std::future<void>> futures;
        futures.reserve(hdr.chunkCount);

        for (uint32_t i = 0; i < hdr.chunkCount; ++i) {
            futures.push_back(pool.enqueue(
                [&algo, in, &chunks, i, &chunkResults, &chunkSuccess]()
                {
                    chunkSuccess[i] = algo.decompress(
                        in + chunks[i].offset,
                        chunks[i].compressedSize,
                        chunkResults[i]);
                }
            ));
        }

        for (auto& f : futures) {
            f.get();
        }
    }

    // ── Verify and assemble ─────────────────────────────────────────
    out.reserve(static_cast<size_t>(hdr.totalOriginalSize));

    for (uint32_t i = 0; i < hdr.chunkCount; ++i) {
        if (!chunkSuccess[i]) {
            std::cerr << "ParallelCompressor: Chunk " << i
                      << " failed to decompress\n";
            return false;
        }
        out.insert(out.end(),
                   chunkResults[i].begin(), chunkResults[i].end());
    }

    // Verify total size matches
    if (out.size() != static_cast<size_t>(hdr.totalOriginalSize)) {
        std::cerr << "ParallelCompressor: Size mismatch — got "
                  << out.size() << ", expected "
                  << hdr.totalOriginalSize << "\n";
        return false;
    }

    return true;
}
