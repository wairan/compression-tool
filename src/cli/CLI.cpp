#include "cli/CLI.hpp"

#include <iostream>   // std::cerr
#include <cstring>    // strcmp
#include <cstdlib>    // strtoul
#include <thread>     // std::thread::hardware_concurrency

// ─────────────────────────────────────────────────────────────────────
// printUsage: human-readable help text
// ─────────────────────────────────────────────────────────────────────
void CLI::printUsage(const char* progName) {
    std::cerr
        << "Usage: " << progName << " [options]\n"
        << "\n"
        << "Required:\n"
        << "  --input  <path>           Input file path\n"
        << "  --output <path>           Output file path\n"
        << "\n"
        << "Options:\n"
        << "  --algo [huffman|lzw|auto] Algorithm (default: auto)\n"
        << "  --decompress              Decompress mode\n"
        << "  --threads <N>             Worker thread count (default: CPU count)\n"
        << "  --chunk-size <bytes>      Chunk size for parallel compression\n"
        << "                            (default: 1048576 = 1 MB)\n"
        << "  --verbose                 Print detailed stats\n"
        << "  --help                    Show this help\n"
        << "\n"
        << "Examples:\n"
        << "  " << progName << " --input data.txt --output data.cmp --verbose\n"
        << "  " << progName << " --input data.cmp --output data.txt --decompress\n"
        << "  " << progName << " --input big.bin --output big.cmp --algo lzw --threads 8\n";
}

// ─────────────────────────────────────────────────────────────────────
// parse: hand-rolled argc/argv parser
//
// Design: simple linear scan of arguments. Each --flag consumes the
// next argument as its value (except flags like --decompress and
// --verbose which are boolean). This is intentionally basic — no
// third-party libraries like getopt_long, boost::program_options,
// or CLI11.
// ─────────────────────────────────────────────────────────────────────
bool CLI::parse(int argc, char* argv[], CLIArgs& args) {
    // Default thread count: let hardware_concurrency decide.
    // Returns 0 if the value is not computable — we'll clamp to 1.
    args.numThreads = std::thread::hardware_concurrency();
    if (args.numThreads == 0) args.numThreads = 1;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return false;
        }
        else if (strcmp(arg, "--input") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --input requires a path argument\n";
                printUsage(argv[0]);
                return false;
            }
            args.inputPath = argv[++i];
        }
        else if (strcmp(arg, "--output") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires a path argument\n";
                printUsage(argv[0]);
                return false;
            }
            args.outputPath = argv[++i];
        }
        else if (strcmp(arg, "--algo") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --algo requires a value "
                          << "[huffman|lzw|auto]\n";
                printUsage(argv[0]);
                return false;
            }
            const char* val = argv[++i];
            if (strcmp(val, "huffman") == 0) {
                args.algo = AlgoMode::Huffman;
            } else if (strcmp(val, "lzw") == 0) {
                args.algo = AlgoMode::LZW;
            } else if (strcmp(val, "auto") == 0) {
                args.algo = AlgoMode::Auto;
            } else {
                std::cerr << "Error: Unknown algorithm '" << val
                          << "'. Use huffman, lzw, or auto.\n";
                printUsage(argv[0]);
                return false;
            }
        }
        else if (strcmp(arg, "--decompress") == 0) {
            args.decompress = true;
        }
        else if (strcmp(arg, "--threads") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --threads requires a number\n";
                printUsage(argv[0]);
                return false;
            }
            char* endPtr = nullptr;
            unsigned long val = strtoul(argv[++i], &endPtr, 10);
            if (*endPtr != '\0' || val == 0) {
                std::cerr << "Error: --threads must be a positive integer\n";
                printUsage(argv[0]);
                return false;
            }
            args.numThreads = static_cast<size_t>(val);
        }
        else if (strcmp(arg, "--chunk-size") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "Error: --chunk-size requires a number\n";
                printUsage(argv[0]);
                return false;
            }
            char* endPtr = nullptr;
            unsigned long val = strtoul(argv[++i], &endPtr, 10);
            if (*endPtr != '\0' || val == 0) {
                std::cerr << "Error: --chunk-size must be a positive integer\n";
                printUsage(argv[0]);
                return false;
            }
            args.chunkSize = static_cast<size_t>(val);
        }
        else if (strcmp(arg, "--verbose") == 0) {
            args.verbose = true;
        }
        else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            printUsage(argv[0]);
            return false;
        }
    }

    // Validate required fields
    if (args.inputPath.empty()) {
        std::cerr << "Error: --input is required\n";
        printUsage(argv[0]);
        return false;
    }
    if (args.outputPath.empty()) {
        std::cerr << "Error: --output is required\n";
        printUsage(argv[0]);
        return false;
    }

    return true;
}
