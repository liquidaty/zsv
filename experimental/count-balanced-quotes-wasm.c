// emcc -O3 -msimd128 count-balanced-quotes-wasm.c -o count-balanced-quotes-wasm.js

#include <stdio.h>
#include <stdint.h>
#include <wasm_simd128.h>

#define BUF_SIZE (64 * 1024)

uint64_t process_buffer(const uint8_t *data, size_t len, int *inside_quote_global) {
    uint64_t total_count = 0;
    size_t i = 0;
    
    // Global state: 0 if currently outside quotes, ~0ULL (all 1s) if inside
    uint64_t quote_state = *inside_quote_global ? ~0ULL : 0ULL;

    wasm_v128_t v_newline = wasm_i8x16_splat('\n');
    wasm_v128_t v_quote   = wasm_i8x16_splat('"');

    // Process 64 bytes at a time (4 x 128-bit vectors)
    for (; i + 64 <= len; i += 64) {
        // 1. Load data
        wasm_v128_t b0 = wasm_v128_load(data + i);
        wasm_v128_t b1 = wasm_v128_load(data + i + 16);
        wasm_v128_t b2 = wasm_v128_load(data + i + 32);
        wasm_v128_t b3 = wasm_v128_load(data + i + 48);

        // 2. Compare and Bitmask
        // wasm_i8x16_bitmask extracts the high bit of every byte into a 16-bit int
        // This is exactly what we needed (and lacked) on NEON!
        uint64_t n0 = wasm_i8x16_bitmask(wasm_i8x16_eq(b0, v_newline));
        uint64_t n1 = wasm_i8x16_bitmask(wasm_i8x16_eq(b1, v_newline));
        uint64_t n2 = wasm_i8x16_bitmask(wasm_i8x16_eq(b2, v_newline));
        uint64_t n3 = wasm_i8x16_bitmask(wasm_i8x16_eq(b3, v_newline));

        uint64_t q0 = wasm_i8x16_bitmask(wasm_i8x16_eq(b0, v_quote));
        uint64_t q1 = wasm_i8x16_bitmask(wasm_i8x16_eq(b1, v_quote));
        uint64_t q2 = wasm_i8x16_bitmask(wasm_i8x16_eq(b2, v_quote));
        uint64_t q3 = wasm_i8x16_bitmask(wasm_i8x16_eq(b3, v_quote));

        // 3. Combine into 64-bit integers
        uint64_t newlines = n0 | (n1 << 16) | (n2 << 32) | (n3 << 48);
        uint64_t quotes   = q0 | (q1 << 16) | (q2 << 32) | (q3 << 48);

        // 4. Prefix XOR (Standard algorithm)
        uint64_t internal_mask = quotes;
        internal_mask ^= (internal_mask << 1);
        internal_mask ^= (internal_mask << 2);
        internal_mask ^= (internal_mask << 4);
        internal_mask ^= (internal_mask << 8);
        internal_mask ^= (internal_mask << 16);
        internal_mask ^= (internal_mask << 32);

        // 5. Apply carry-over state
        uint64_t final_inside_mask = internal_mask ^ quote_state;

        // 6. Count valid newlines
        total_count += __builtin_popcountll(newlines & ~final_inside_mask);

        // 7. Update global state
        if (__builtin_popcountll(quotes) & 1) {
            quote_state = ~quote_state;
        }
    }

    *inside_quote_global = (quote_state != 0);

    // Tail handling
    for (; i < len; i++) {
        if (data[i] == '"') *inside_quote_global = !(*inside_quote_global);
        else if (data[i] == '\n' && !(*inside_quote_global)) total_count++;
    }

    return total_count;
}

int main() {
    static uint8_t buffer[BUF_SIZE];
    uint64_t total_newlines = 0;
    int inside_quote = 0;
    size_t bytes_read;

    // Emscripten handles stdin correctly in Node.js
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, stdin)) > 0) {
        total_newlines += process_buffer(buffer, bytes_read, &inside_quote);
    }

    printf("%llu\n", total_newlines);
    return 0;
}
