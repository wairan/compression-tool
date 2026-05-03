#ifndef PARALLEL_COMPRESSOR_HPP
#define PARALLEL_COMPRESSOR_HPP

// ═══════════════════════════════════════════════════════════════════════
// ParallelCompressor — Multithreaded chunk-based compression
//
// Design Note: Why assemble results AFTER join (not during)?
// ─────────────────────────────────────────────────────────────────────
// Each worker thread compresses its chunk into a LOCAL buffer (a
// per-chunk std::vector<uint8_t>). After ALL futures are resolved
// (all workers done), the main thread assembles chunks into the
// final output in original order.
//
// The alternative — having workers write directly into a shared output
// buffer — would require either:
//   (a) Pre-computing each chunk's compressed size to calculate offsets
//       (impossible without actually compressing), or
//   (b) A mutex-guarded append to the output vector, which serializes
//       the writes and creates lock contention.
//
// Our approach has ZERO shared mutable state during compression:
//   - Each chunk's output buffer is owned solely by one worker thread
//   - No mutex is needed during the hot compression loop
//   - Assembly is a simple sequential memcpy after all work is done
//
// The data race we avoid: multiple threads writing to the same vector
// would cause concurrent reallocation (vector::push_back may reallocate
// the internal array), leading to use-after-free and torn writes.
// Even with pre-allocated space, concurrent writes to adjacent memory
// regions can cause false sharing (cache line bouncing between cores).
// ═══════════════════════════════════════════════════════════════════════

#include "algorithms/ICompressor.hpp"
#include <cstdint>
#include <vector>

class ParallelCompressor {
public:
    // Default chunk size: 256 KB
    // Chosen as a balance between parallelism granularity and per-chunk
    // compression overhead. Too small → header overhead dominates.
    // Too large → fewer chunks than threads → underutilization.
    static constexpr size_t DEFAULT_CHUNK_SIZE = 256 * 1024;

    // Compress input using multiple threads.
    // algo:       the compression algorithm to use per-chunk
    // algoId:     algorithm ID for the parallel header (0x01 or 0x02)
    // in/inSize:  raw input data
    // out:        output buffer (cleared and filled)
    // chunkSize:  bytes per chunk (last chunk may be smaller)
    // numThreads: number of worker threads in the pool
    static bool compress(ICompressor& algo, uint8_t algoId,
                         const uint8_t* in, size_t inSize,
                         std::vector<uint8_t>& out,
                         size_t chunkSize = DEFAULT_CHUNK_SIZE,
                         size_t numThreads = 4);

    // Decompress a parallel-compressed buffer.
    // Reads the parallel header, decompresses each chunk, concatenates.
    static bool decompress(ICompressor& algo,
                           const uint8_t* in, size_t inSize,
                           std::vector<uint8_t>& out,
                           size_t numThreads = 4);
};

#endif // PARALLEL_COMPRESSOR_HPP
