#include "io/MmapReader.hpp"

// ═══════════════════════════════════════════════════════════════════════
// POSIX headers — these replace <fstream> / <cstdio> entirely.
//
// We deliberately avoid C++ file streams here because:
//   - They add buffering layers we don't need (mmap IS the access).
//   - They don't expose the file descriptor needed for mmap().
//   - They hide the OS-level error semantics (errno vs. exceptions).
// ═══════════════════════════════════════════════════════════════════════
#include <sys/mman.h>    // mmap(), munmap(), MAP_SHARED, PROT_READ    [POSIX]
#include <sys/stat.h>    // fstat() — retrieve file size                [POSIX]
#include <fcntl.h>       // open(), O_RDONLY                            [POSIX]
#include <unistd.h>      // close()                                     [POSIX]
#include <cerrno>        // errno — POSIX error reporting
#include <cstring>       // strerror() — human-readable errno strings
#include <stdexcept>     // std::runtime_error
#include <iostream>      // std::cerr — for diagnostic logging

// ─────────────────────────────────────────────────────────────────────
// Constructor: open → fstat → mmap
// Each syscall's return value is checked immediately. On failure we
// clean up anything already acquired (RAII within the constructor).
// ─────────────────────────────────────────────────────────────────────
MmapReader::MmapReader(const std::string& filepath)
    : filepath_(filepath)
    , fd_(-1)                   // -1 = no valid descriptor yet
    , mapped_region_(MAP_FAILED) // MAP_FAILED = sentinel for "not mapped"
    , file_size_(0)
{
    // ── Step 1: Open the file ───────────────────────────────────────
    // O_RDONLY: We only need read access for compression input.
    // Why open() instead of fopen()?
    //   → mmap() requires a raw file descriptor (int fd), not a FILE*.
    //   → open() gives us direct control over flags and permissions.
    fd_ = open(filepath_.c_str(), O_RDONLY);
    if (fd_ == -1) {
        // errno is set by open(). Common errors:
        //   ENOENT — file does not exist
        //   EACCES — permission denied
        throw std::runtime_error(
            "MmapReader: Failed to open '" + filepath_ + "': " +
            strerror(errno)
        );
    }

    // ── Step 2: Get file size via fstat ─────────────────────────────
    // Why fstat instead of lseek(fd, 0, SEEK_END)?
    //   → fstat is a single syscall that returns all metadata at once.
    //   → lseek modifies the file offset (which mmap doesn't use, but
    //     it's still a side effect we'd rather avoid).
    struct stat file_info;
    if (fstat(fd_, &file_info) == -1) {
        int saved_errno = errno;
        close(fd_);  // Clean up the fd we already opened
        throw std::runtime_error(
            "MmapReader: fstat failed for '" + filepath_ + "': " +
            strerror(saved_errno)
        );
    }

    file_size_ = static_cast<size_t>(file_info.st_size);

    // ── Edge case: empty file ───────────────────────────────────────
    // mmap with length=0 is undefined behavior on most POSIX systems
    // (Linux returns EINVAL). We handle this gracefully by leaving
    // mapped_region_ as MAP_FAILED and treating data() as nullptr.
    // Callers should check size() == 0 before accessing data().
    if (file_size_ == 0) {
        std::cerr << "MmapReader: Warning — '" << filepath_
                  << "' is an empty file (0 bytes). No mapping created.\n";
        // fd_ remains open (closed in destructor), but no mmap needed.
        mapped_region_ = nullptr;  // Distinct from MAP_FAILED — intentional
        return;
    }

    // ── Step 3: Memory-map the file ─────────────────────────────────
    // Argument breakdown:
    //   nullptr   → let the kernel choose the virtual address (most flexible)
    //   file_size → map the entire file
    //   PROT_READ → read-only access; writing to this region would SIGSEGV
    //   MAP_PRIVATE → we don't need changes visible to other processes;
    //                 MAP_PRIVATE lets the kernel optimize (copy-on-write
    //                 pages). We could use MAP_SHARED for read-only access
    //                 too, but MAP_PRIVATE is the safer default since it
    //                 guarantees our view won't be affected if another
    //                 process modifies the file.
    //
    // Design Note: MAP_PRIVATE vs MAP_SHARED
    //   For read-only access, both behave identically. MAP_PRIVATE is
    //   chosen because it's semantically correct ("we want a private copy")
    //   and avoids accidental write-through if PROT_WRITE were ever added.
    //   However, MAP_SHARED would also work fine here since PROT_READ
    //   prevents writes regardless.
    mapped_region_ = mmap(
        nullptr,         // addr: kernel chooses
        file_size_,      // length: entire file
        PROT_READ,       // protection: read-only
        MAP_PRIVATE,     // flags: private mapping (see design note above)
        fd_,             // file descriptor
        0                // offset: start from beginning of file
    );

    if (mapped_region_ == MAP_FAILED) {
        int saved_errno = errno;
        close(fd_);  // Clean up fd on mmap failure
        throw std::runtime_error(
            "MmapReader: mmap failed for '" + filepath_ + "' (" +
            std::to_string(file_size_) + " bytes): " +
            strerror(saved_errno)
        );
    }

    // ── Optional: advise the kernel about our access pattern ────────
    // madvise(mapped_region_, file_size_, MADV_SEQUENTIAL) would tell
    // the kernel to aggressively read-ahead pages. We omit this for now
    // because multi-threaded chunk access (Step 3) will be random.
    // If profiling shows read stalls, this is the first knob to turn.
}

// ─────────────────────────────────────────────────────────────────────
// Destructor: munmap → close
//
// RAII guarantee: even if an exception propagates through code using
// MmapReader, the destructor is called during stack unwinding, ensuring
// no leaked mappings or file descriptors.
//
// Order matters: munmap before close. Although the POSIX spec allows
// either order (the mapping holds a kernel reference to the inode),
// unmapping first is the conventional and safest pattern.
// ─────────────────────────────────────────────────────────────────────
MmapReader::~MmapReader() {
    // munmap: release the virtual address range back to the kernel.
    // Only call if we actually mapped something (not empty file, not failed).
    if (mapped_region_ != nullptr && mapped_region_ != MAP_FAILED) {
        if (munmap(mapped_region_, file_size_) == -1) {
            // Destructors must not throw. Log and continue.
            // munmap failure is extremely rare (usually a bug — wrong address
            // or size). In production, this would trigger an alert.
            std::cerr << "MmapReader: WARNING — munmap failed for '"
                      << filepath_ << "': " << strerror(errno) << "\n";
        }
    }

    // close: release the file descriptor.
    // Even after munmap, the fd must be explicitly closed.
    // Failing to close leaks a descriptor slot in the process's fd table.
    if (fd_ != -1) {
        if (close(fd_) == -1) {
            std::cerr << "MmapReader: WARNING — close failed for '"
                      << filepath_ << "': " << strerror(errno) << "\n";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// Accessors — trivial, no locking needed (immutable after construction)
// ─────────────────────────────────────────────────────────────────────

const uint8_t* MmapReader::data() const noexcept {
    // Cast from void* (mmap's return type) to uint8_t* (byte-level access).
    // This is safe because we're interpreting raw bytes, not typed objects.
    return static_cast<const uint8_t*>(mapped_region_);
}

size_t MmapReader::size() const noexcept {
    return file_size_;
}

const std::string& MmapReader::filepath() const noexcept {
    return filepath_;
}
