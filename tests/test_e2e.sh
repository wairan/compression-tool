#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════
# test_e2e.sh — End-to-end tests for the compressor binary
#
# Usage: ./tests/test_e2e.sh [path-to-compressor-binary]
#        Default binary path: ./compressor
#
# Tests:
#   1. Auto-mode: compress text file → decompress → diff
#   2. Forced LZW: compress random data → decompress → diff
#   3. Parallel: compress with --threads 4 → decompress → diff
#   4. Bad input: decompress a non-compressed file → expect exit 1
#
# Exit code: 0 if all pass, 1 if any fail.
# ═══════════════════════════════════════════════════════════════════════

set -euo pipefail

BINARY="${1:-./compressor}"
TMPDIR=$(mktemp -d)
PASSED=0
FAILED=0

cleanup() {
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

pass() {
    echo "  ✓ PASS: $1"
    PASSED=$((PASSED + 1))
}

fail() {
    echo "  ✗ FAIL: $1"
    FAILED=$((FAILED + 1))
}

echo "=== End-to-End Tests ==="
echo "Binary: $BINARY"
echo "Temp dir: $TMPDIR"

# ─── Test 1: Auto-mode text compression ─────────────────────────────
echo ""
echo "--- Test 1: Auto-mode (text file) ---"

# Create a 10 KB text file with repeated patterns
python3 -c "
text = 'The operating system kernel manages hardware resources. ' * 200
print(text)" > "$TMPDIR/input_text.txt"

"$BINARY" --input "$TMPDIR/input_text.txt" \
          --output "$TMPDIR/text.cmp" \
          --algo auto --verbose \
          --threads 1 2>&1 && COMP_OK=true || COMP_OK=false

if [ "$COMP_OK" = true ]; then
    pass "Compression succeeded"
else
    fail "Compression failed"
fi

"$BINARY" --input "$TMPDIR/text.cmp" \
          --output "$TMPDIR/text_restored.txt" \
          --decompress --verbose 2>&1 && DEC_OK=true || DEC_OK=false

if [ "$DEC_OK" = true ]; then
    pass "Decompression succeeded"
else
    fail "Decompression failed"
fi

if diff -q "$TMPDIR/input_text.txt" "$TMPDIR/text_restored.txt" > /dev/null 2>&1; then
    pass "Round-trip: output matches original"
else
    fail "Round-trip: output differs from original"
fi

# ─── Test 2: Forced LZW on random data ──────────────────────────────
echo ""
echo "--- Test 2: Forced LZW (random data) ---"

# Create 4 KB of random data
dd if=/dev/urandom of="$TMPDIR/input_random.bin" bs=1024 count=4 2>/dev/null

"$BINARY" --input "$TMPDIR/input_random.bin" \
          --output "$TMPDIR/random.cmp" \
          --algo lzw --verbose \
          --threads 1 2>&1 && COMP_OK=true || COMP_OK=false

if [ "$COMP_OK" = true ]; then
    pass "LZW compression succeeded"
else
    fail "LZW compression failed"
fi

"$BINARY" --input "$TMPDIR/random.cmp" \
          --output "$TMPDIR/random_restored.bin" \
          --decompress --verbose 2>&1 && DEC_OK=true || DEC_OK=false

if [ "$DEC_OK" = true ]; then
    pass "LZW decompression succeeded"
else
    fail "LZW decompression failed"
fi

if diff -q "$TMPDIR/input_random.bin" "$TMPDIR/random_restored.bin" > /dev/null 2>&1; then
    pass "LZW round-trip: output matches original"
else
    fail "LZW round-trip: output differs from original"
fi

# ─── Test 3: Parallel compression (4 threads) ───────────────────────
echo ""
echo "--- Test 3: Parallel compression (4 threads) ---"

# Create 1 MB test file
dd if=/dev/zero of="$TMPDIR/input_parallel.bin" bs=1024 count=1024 2>/dev/null

"$BINARY" --input "$TMPDIR/input_parallel.bin" \
          --output "$TMPDIR/parallel.cmp" \
          --algo huffman --threads 4 --verbose 2>&1 && COMP_OK=true || COMP_OK=false

if [ "$COMP_OK" = true ]; then
    pass "Parallel compression succeeded"
else
    fail "Parallel compression failed"
fi

"$BINARY" --input "$TMPDIR/parallel.cmp" \
          --output "$TMPDIR/parallel_restored.bin" \
          --decompress --threads 4 --verbose 2>&1 && DEC_OK=true || DEC_OK=false

if [ "$DEC_OK" = true ]; then
    pass "Parallel decompression succeeded"
else
    fail "Parallel decompression failed"
fi

if diff -q "$TMPDIR/input_parallel.bin" "$TMPDIR/parallel_restored.bin" > /dev/null 2>&1; then
    pass "Parallel round-trip: output matches original"
else
    fail "Parallel round-trip: output differs from original"
fi

# ─── Test 4: Bad input (not a compressed file) ──────────────────────
echo ""
echo "--- Test 4: Bad input (expect failure) ---"

echo "This is definitely not a compressed file." > "$TMPDIR/not_compressed.txt"

# Capture stderr and check for error message + non-zero exit
if "$BINARY" --input "$TMPDIR/not_compressed.txt" \
             --output "$TMPDIR/should_not_exist.bin" \
             --decompress 2>"$TMPDIR/stderr.txt"; then
    fail "Decompressing bad file should have failed but exited 0"
else
    # Check that an error message was written to stderr
    if [ -s "$TMPDIR/stderr.txt" ]; then
        pass "Bad input rejected with error (exit 1)"
        echo "    stderr: $(cat "$TMPDIR/stderr.txt" | head -1)"
    else
        fail "Bad input rejected but no error message on stderr"
    fi
fi

# ─── Summary ────────────────────────────────────────────────────────
echo ""
echo "=============================="
echo "  Passed: $PASSED"
echo "  Failed: $FAILED"
echo "=============================="

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
exit 0
