#ifndef FILE_WRITER_HPP
#define FILE_WRITER_HPP

// ═══════════════════════════════════════════════════════════════════════
// FileWriter — POSIX write()-based output class
//
// OS RATIONALE — Why NOT mmap for output?
// ─────────────────────────────────────────────────────────────────────
// 1. Output size is unknown at start: Compression produces a result
//    whose size we can't predict before running the algorithm. mmap
//    requires the file to be pre-sized (via ftruncate), which would
//    mean either over-allocating (wasting disk) or under-allocating
//    (requiring mremap, which is Linux-only and not portable).
//
// 2. Sequential write pattern: Compressed output is produced as a
//    stream — header first, then data chunks. write() is optimal for
//    sequential appending; it lets the kernel manage page cache
//    write-back efficiently.
//
// 3. Simpler error handling: write() returns byte count or -1.
//    mmap'd writes can silently corrupt if the filesystem is full
//    (deferred SIGBUS on write-back), making error recovery harder.
// ═══════════════════════════════════════════════════════════════════════

#include <cstdint>   // uint8_t, size_t
#include <string>    // std::string

class FileWriter {
public:
    // Opens the file for writing (creates if not exists, truncates if exists).
    explicit FileWriter(const std::string& filepath);

    // RAII: closes the file descriptor.
    ~FileWriter();

    // Deleted copy/move — same rationale as MmapReader (fd ownership).
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
    FileWriter(FileWriter&&) = delete;
    FileWriter& operator=(FileWriter&&) = delete;

    // Write a buffer of `length` bytes to the file.
    // Handles partial writes by looping (POSIX write may return short).
    // Returns true on success, false on error.
    bool write(const uint8_t* buffer, size_t length);

    // Write a single byte — convenience for header construction.
    bool writeByte(uint8_t byte);

    // Flush any kernel-buffered data to disk (calls fsync).
    // Important before reporting "compression complete" to the user,
    // because write() only guarantees data reaches the page cache,
    // not the physical disk.
    bool flush();

    // Get the total number of bytes written so far.
    size_t bytesWritten() const noexcept;

    // Get the filepath.
    const std::string& filepath() const noexcept;

private:
    std::string filepath_;
    int         fd_;              // POSIX file descriptor
    size_t      bytes_written_;   // Running total
};

#endif // FILE_WRITER_HPP
