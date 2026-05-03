# FileCompressor Web UI

This is the web UI for the FileCompressor project, providing a user-friendly way to compress and decompress files using the custom C++ engine.

## Setup Instructions

1. **Build the C++ binary:**
   Ensure you have built the C++ backend first using `cmake` and `make`.
   ```bash
   cd ..
   mkdir -p build && cd build
   cmake ..
   make -j$(nproc)
   ```

2. **Configure the path:**
   The backend relies on the `COMPRESSOR_BIN` environment variable to find the compiled binary. By default, it looks in `../../compressor` or `../../build/compressor`. If your binary is elsewhere, set the environment variable.

3. **Install dependencies and start:**
   From the `ui` directory, run:
   ```bash
   npm install
   npm run dev
   ```
   This will start both the Express backend (port 3001) and the React frontend (port 3000).

## Screenshots

*(Placeholder for UI screenshots)*

### Drag and Drop Interface
![Drag and Drop](placeholder)

### Compression Results
![Results](placeholder)
