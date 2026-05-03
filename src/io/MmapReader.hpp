#ifndef MMAP_READER_HPP
#define MMAP_READER_HPP

// ═══════════════════════════════════════════════════════════════════════
// MmapReader — Memory-mapped file reader (POSIX)
//
// OS RATIONALE — Why mmap instead of fread/fgets?
// ─────────────────────────────────────────────────────────────────────
// 1. ZERO-COPY I/O: mmap maps the file directly into the process's
//    virtual address space. The kernel manages page faults to load data
//    on demand, eliminating the explicit read() → user-buffer copy.
//    For large files this avoids double-buffering (kernel page cache →
//    user buffer) that fread/fgets would incur.
//
// 2. DEMAND PAGING: Only the pages actually accessed are loaded into
//    physical RAM. If we only need to sample the first 8 KB for entropy
//    analysis (Step 4), the rest of the file is never faulted in.
//
// 3. SIMPLIFIED RANDOM ACCESS: With mmap the file looks like a flat
//    byte array. Multi-threaded chunk processing (Step 3) can partition
//    the array by index without coordinating file-pointer seeks.
//
// 4. KERNEL-LEVEL CACHING: mmap'd pages participate directly in the
//    kernel's unified page cache, which is shared across processes and
//    avoids redundant disk reads.
//
// TRADE-OFFS:
//   - mmap has higher setup cost than a single sequential fread for
//     very small files (< 4 KB). We accept this since compression is
//     meaningless for tiny files anyway.
//   - Error handling is more nuanced: SIGBUS can occur if the file is
//     truncated while mapped. In a production tool we'd install a
//     signal handler; for this project we assume stable input files.
//   - Not available on native Windows (would need CreateFileMapping).
//     This code is POSIX-only by design.
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>   // uint8_t, size_t
#include <string>    // std::string for file path

// [POSIX-ONLY] — mmap, munmap, open, close, stat are POSIX APIs.
// On Windows, you would use CreateFileMapping / MapViewOfFile instead.

class MmapReader {
public:
    // ─── Constructor ────────────────────────────────────────────────
    // Opens the file and mmap's it into memory.
    // Throws std::runtime_error on failure (file not found, mmap fail).
    explicit MmapReader(const std::string& filepath);

    // ─── Destructor (RAII) ──────────────────────────────────────────
    // Guarantees munmap + close even on exception unwind paths.
    // This is critical: leaked mappings consume virtual address space
    // and keep the kernel inode reference alive, preventing deletion.
    ~MmapReader();

    // ─── Deleted copy/move ──────────────────────────────────────────
    // Copying an mmap handle is dangerous (double munmap). Moving
    // would require nullifying the source. We disable both for safety;
    // pass by pointer/reference instead.
    MmapReader(const MmapReader&) = delete;
    MmapReader& operator=(const MmapReader&) = delete;
    MmapReader(MmapReader&&) = delete;
    MmapReader& operator=(MmapReader&&) = delete;

    // ─── Accessors ──────────────────────────────────────────────────
    // data(): pointer to the first byte of the mapped region.
    //         Valid for the lifetime of this MmapReader object.
    const uint8_t* data() const noexcept;

    // size(): number of bytes in the file (== mapping length).
    size_t size() const noexcept;

    // filepath(): returns the path used to open the file.
    const std::string& filepath() const noexcept;

private:
    std::string filepath_;       // Path passed to constructor
    int         fd_;             // POSIX file descriptor from open()
    void*       mapped_region_;  // Pointer returned by mmap()
    size_t      file_size_;      // File size from fstat()
};

#endif // MMAP_READER_HPP
