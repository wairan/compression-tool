#include "io/FileWriter.hpp"

// ═══════════════════════════════════════════════════════════════════════
// POSIX I/O headers — raw syscalls for maximum control over output.
// ═══════════════════════════════════════════════════════════════════════
#include <fcntl.h>       // open(), O_WRONLY, O_CREAT, O_TRUNC          [POSIX]
#include <unistd.h>      // write(), close(), fsync()                   [POSIX]
#include <cerrno>        // errno
#include <cstring>       // strerror()
#include <stdexcept>     // std::runtime_error
#include <iostream>      // std::cerr

// ─────────────────────────────────────────────────────────────────────
// Constructor: open file for writing
//
// Flags:
//   O_WRONLY — write-only (we never read the output file)
//   O_CREAT  — create the file if it doesn't exist
//   O_TRUNC  — if the file exists, truncate it to zero length
//              (we're writing a fresh compressed output)
//
// Mode 0644:
//   Owner: read+write, Group: read, Others: read
//   Standard permission for user-created data files on Unix.
// ─────────────────────────────────────────────────────────────────────
FileWriter::FileWriter(const std::string& filepath)
    : filepath_(filepath)
    , fd_(-1)
    , bytes_written_(0)
{
    fd_ = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ == -1) {
        throw std::runtime_error(
            "FileWriter: Failed to open '" + filepath_ + "' for writing: " +
            strerror(errno)
        );
    }
}

// ─────────────────────────────────────────────────────────────────────
// Destructor: close the fd. RAII ensures no fd leak on exceptions.
// ─────────────────────────────────────────────────────────────────────
FileWriter::~FileWriter() {
    if (fd_ != -1) {
        // Note: we do NOT fsync here automatically — flushing to disk is
        // an explicit choice (performance vs. durability trade-off).
        // The caller should call flush() if durability is required.
        if (close(fd_) == -1) {
            std::cerr << "FileWriter: WARNING — close failed for '"
                      << filepath_ << "': " << strerror(errno) << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// write(): handles partial writes
//
// POSIX write() is allowed to write fewer bytes than requested
// (especially for pipes, sockets, or under memory pressure). We loop
// until all bytes are written or an error occurs.
//
// This is a critical correctness detail that many student projects miss:
// a single write() call is NOT guaranteed to write the full buffer.
// ─────────────────────────────────────────────────────────────────────
bool FileWriter::write(const uint8_t* buffer, size_t length) {
    size_t total_written = 0;

    while (total_written < length) {
        // ssize_t: signed size type; write() returns -1 on error
        ssize_t result = ::write(
            fd_,
            buffer + total_written,
            length - total_written
        );

        if (result == -1) {
            if (errno == EINTR) {
                // EINTR: write was interrupted by a signal before any data
                // was written. This is not an error — just retry.
                // Common in programs that use signal handlers (e.g., SIGCHLD).
                continue;
            }
            std::cerr << "FileWriter: write failed for '" << filepath_
                      << "': " << strerror(errno) << "\n";
            return false;
        }

        total_written += static_cast<size_t>(result);
    }

    bytes_written_ += total_written;
    return true;
}

// ─────────────────────────────────────────────────────────────────────
// writeByte(): convenience wrapper for single-byte writes.
// Used primarily during header construction (magic bytes, algo ID).
// ─────────────────────────────────────────────────────────────────────
bool FileWriter::writeByte(uint8_t byte) {
    return write(&byte, 1);
}

// ─────────────────────────────────────────────────────────────────────
// flush(): fsync the file descriptor
//
// fsync() forces the kernel to write all buffered data for this fd
// to the physical storage device. Without this, data may sit in the
// kernel's page cache and be lost on power failure.
//
// Design Note: We use fsync() rather than fdatasync() because we want
// to guarantee metadata (file size, timestamps) is also flushed.
// fdatasync() skips metadata and is faster, but for a compression tool
// the file size must be accurate on disk.
// ─────────────────────────────────────────────────────────────────────
bool FileWriter::flush() {
    if (fsync(fd_) == -1) {
        std::cerr << "FileWriter: fsync failed for '" << filepath_
                  << "': " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

size_t FileWriter::bytesWritten() const noexcept {
    return bytes_written_;
}

const std::string& FileWriter::filepath() const noexcept {
    return filepath_;
}
