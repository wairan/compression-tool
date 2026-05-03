// BinaryFormat.cpp — Stub (Step 3/4)
// Will implement the custom compressed file format:
//   - Magic bytes: "CMPX" (4 bytes)
//   - Version: 1 byte
//   - Algorithm ID: 1 byte (0x01 = Huffman, 0x02 = LZW)
//   - Original file size: 8 bytes (uint64_t, little-endian)
//   - Metadata length: 4 bytes (uint32_t)
//   - Metadata: variable (Huffman tree or LZW dictionary)
//   - Compressed data: remainder of file
