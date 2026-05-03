# FileCompressor Architecture

This document describes the high-level architecture, design decisions, and data flows of the FileCompressor project.

## Component Diagram

```text
+--------------------------------------------------------------------------------+
|                                    USER                                        |
|                                     |                                          |
|  [Drag & Drop] / [Download]         |       (HTTP/REST over localhost:3000)    |
|                                     V                                          |
| +----------------------------------------------------------------------------+ |
| |                            React Frontend (Vite)                           | |
| |  - DropZone, OptionsPanel, ActionButton, ResultCard                        | |
| +----------------------------------------------------------------------------+ |
|                                     |                                          |
|  (Multipart Form-Data) POST /compress                                          |
|                                     V                                          |
| +----------------------------------------------------------------------------+ |
| |                           Express Backend (Node.js)                        | |
| |  - Saves tmp file via multer                                               | |
| |  - child_process.spawn()                                                   | |
| |  - Streams stderr/stdout, serves output file                               | |
| +----------------------------------------------------------------------------+ |
|                                     |                                          |
|  (Process Execution) --input tmp.bin --algo auto --threads 4                 |
|                                     V                                          |
| +----------------------------------------------------------------------------+ |
| |                            C++ Binary Engine                               | |
| |                                                                            | |
| |  [ CLI Parser ] -> [ MmapReader ] -> [ EntropyAnalyzer ]                   | |
| |                                           |                                | |
| |    +--------------------------------------+---------------------------+    | |
| |    |                                                                  |    | |
| |    V (Single Thread)                                  (Multi Thread)  V    | |
| | [ Huffman / LZW ]                                     [ ThreadPool ]       | |
| |                                                             |              | |
| |    |                                                        V              | |
| |    |                                             [ ParallelCompressor ]    | |
| |    |                                                        |              | |
| |    V                                                        V              | |
| | [ FileWriter ] <--------------------------------------------+              | |
| +----------------------------------------------------------------------------+ |
+--------------------------------------------------------------------------------+
```

## Data Flow

1. **File Input:** User drags and drops a file in the React frontend.
2. **Network Request:** React sends a `multipart/form-data` POST request to Express.
3. **Temp Storage:** Express uses `multer` to stream the uploaded file to a temporary location with a UUID.
4. **Execution:** Express invokes the C++ binary using `child_process.spawn()`. `spawn` is used over `exec` to avoid buffer overflow limits and securely pass arguments without shell evaluation.
5. **C++ Processing:**
   - **I/O:** `MmapReader` memory-maps the input file (zero-copy read).
   - **Analysis:** `EntropyAnalyzer` reads the first 8KB to calculate Shannon entropy.
   - **Compression:** Based on CLI flags, the data is compressed either single-threaded or parallel chunked using `Huffman` or `LZW`.
   - **Output:** The result is written using `FileWriter` with an appropriate header (`PRLZ` or `CF`).
6. **Delivery:** The C++ binary exits with code 0. Express sends the resulting output file back to the React client via `res.download()`, and immediately unlinks (deletes) the temporary input and output files.
7. **Client Rendering:** React prompts the user to download the file and displays performance statistics.

## Algorithm Selection Logic

The `EntropyAnalyzer` calculates the Shannon entropy $H$ of the file's first 8KB. Entropy measures the average "information content" or "surprise" per byte, ranging from 0 to 8 bits/byte.

- **Threshold ($H < 4.0$):** Recommends **Huffman**. Low entropy indicates heavily skewed byte frequencies (e.g., highly structured text, arrays of zeros). Huffman is optimal here as it assigns very short bit codes to frequent bytes.
- **Threshold ($H \ge 4.0$):** Recommends **LZW**. Moderate to high entropy means byte frequencies are more uniform, but LZW excels at identifying multi-byte *sequential* patterns (e.g., binary files, mixed media) which Huffman ignores.

## Thread Pool Design

The `ThreadPool` employs a fixed number of worker threads (defaulting to system hardware concurrency) reading tasks from a synchronized `std::deque`. It uses `std::condition_variable` with lambda predicates to avoid CPU busy-waiting and protect against spurious wakeups. Tasks are enqueued via `std::packaged_task`, returning a `std::future` for exception propagation.

### Why Chunk Assembly is Post-Join
During parallel compression via `ParallelCompressor`:
1. The input is sliced into equal-sized chunks (e.g., 256 KB).
2. Each worker thread compresses its chunk into its *own* local `std::vector<uint8_t>`.
3. The main thread `join`s (waits on futures) for all threads to finish.
4. Finally, the main thread sequentially appends each chunk's vector into the final output.

**Rationale:** If threads tried to write directly to a single shared output vector concurrently, it would require a mutex (causing lock contention and serializing writes) or pre-allocating exact sizes (which is impossible since compressed sizes are unknown). Post-join assembly ensures **zero shared mutable state** during the hot compression loop, entirely avoiding data races and false sharing cache penalties.

## Binary File Formats

Both formats use little-endian byte order for multi-byte integers to avoid byte-swap overhead on x86/x86-64 platforms.

### 1. Single-Threaded Format (`CF`)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 2 bytes | Magic | `"CF"` |
| 2 | 1 byte | Algo ID | `0x01` (Huffman), `0x02` (LZW) |
| 3 | 8 bytes | Original Size | Uncompressed size (uint64_t LE) |
| 11 | N bytes | Payload | Compressed data (Huffman or LZW specific) |

### 2. Parallel Format (`PRLZ`)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 bytes | Magic | `"PRLZ"` |
| 4 | 1 byte | Algo ID | `0x01` (Huffman), `0x02` (LZW) |
| 5 | 4 bytes | Chunk Count | Number of chunks (uint32_t LE) |
| 9 | 4 bytes | Chunk Size | Max uncompressed bytes per chunk (uint32_t LE) |
| 13 | 8 bytes | Original Size | Total uncompressed size (uint64_t LE) |
| 21 | ... | Chunk Table/Data | For each chunk: 4 bytes (compressed size, uint32_t LE) followed by compressed payload. |

## Known Limitations & Future Improvements

- **Huffman Table Overhead:** The current Huffman implementation serializes its entire tree/frequency table per chunk. For small chunks, this overhead can dominate compression gains. *Improvement: Canonical Huffman encoding or a shared global tree for parallel chunks.*
- **LZW Dictionary Reset:** LZW uses a fixed 4096-entry dictionary (12-bit codes). It simply stops adding to the dictionary when full. *Improvement: Clear the dictionary when compression ratio degrades (Variable-Width LZW).*
- **In-Memory Assembly:** ParallelCompressor assembles the entire output in RAM before writing. *Improvement: Stream completed chunks to disk sequentially to reduce peak memory usage.*
- **Network Overhead:** The Web UI transmits the entire file to the Express backend and back. *Improvement: Port the C++ algorithms to WebAssembly (Wasm) to compress entirely client-side in the browser.*
