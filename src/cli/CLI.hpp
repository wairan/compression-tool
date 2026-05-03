#ifndef CLI_HPP
#define CLI_HPP

// ═══════════════════════════════════════════════════════════════════════
// CLI — Command-line argument parser (hand-rolled, no third-party libs)
//
// Supported flags:
//   --input  <path>                Input file path (required)
//   --output <path>                Output file path (required)
//   --algo   [huffman|lzw|auto]    Algorithm selection (default: auto)
//   --decompress                   Decompress mode (flag, no value)
//   --threads <N>                  Worker threads (default: CPU count)
//   --chunk-size <bytes>           Chunk size for parallel (default: 1 MB)
//   --verbose                      Print stats to stdout
//   --help                         Print usage and exit
// ═══════════════════════════════════════════════════════════════════════

#include <string>
#include <cstddef>

// Algorithm mode as specified by the user
enum class AlgoMode {
    Auto,     // Decide based on entropy analysis
    Huffman,  // Force Huffman
    LZW       // Force LZW
};

// Parsed CLI arguments — all fields are typed, no string parsing needed downstream
struct CLIArgs {
    std::string inputPath;
    std::string outputPath;
    AlgoMode    algo         = AlgoMode::Auto;
    bool        decompress   = false;
    size_t      numThreads   = 0;  // 0 = auto-detect (hardware_concurrency)
    size_t      chunkSize    = 1048576;  // 1 MB default
    bool        verbose      = false;
};

class CLI {
public:
    // Parse command-line arguments. Returns true on success.
    // On failure: prints usage to stderr and returns false.
    static bool parse(int argc, char* argv[], CLIArgs& args);

    // Print usage information to stderr.
    static void printUsage(const char* progName);
};

#endif // CLI_HPP
