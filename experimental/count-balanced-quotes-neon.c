// gcc -O3 -march=native -o count-balanced-quotes-neon count-balanced-quotes-neon.c

#include <stdio.h>
#include <stdint.h>
#include <arm_neon.h>

#define BUF_SIZE (256 * 1024)

inline uint16_t neon_movemask(uint8x16_t input) {
    // Weights for each byte to position them into a binary sum
    const uint8x16_t bit_weights = {
        1, 2, 4, 8, 16, 32, 64, 128, 
        1, 2, 4, 8, 16, 32, 64, 128
    };
    
    // 0xFF where match, 0x00 elsewhere -> becomes bit weight or 0
    uint8x16_t masked = vandq_u8(input, bit_weights);

    // Split into low and high 64-bit halves
    uint8x8_t low = vget_low_u8(masked);
    uint8x8_t high = vget_high_u8(masked);

    // Sum the bytes in each half. 
    // Since weights are powers of 2 (1..128), the sum creates the bitmask.
    return vaddv_u8(low) | (vaddv_u8(high) << 8);
}

uint64_t process_buffer(const uint8_t *data, size_t len, int *inside_quote_global) {
    uint64_t total_count = 0;
    size_t i = 0;
    
    // Global state: 0 if currently outside quotes, ~0ULL (all 1s) if inside
    uint64_t quote_state = *inside_quote_global ? ~0ULL : 0ULL;

    // Process 64 bytes at a time
    for (; i + 64 <= len; i += 64) {
        uint8x16_t b0 = vld1q_u8(data + i);
        uint8x16_t b1 = vld1q_u8(data + i + 16);
        uint8x16_t b2 = vld1q_u8(data + i + 32);
        uint8x16_t b3 = vld1q_u8(data + i + 48);

        uint64_t n0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8('\n')));
        uint64_t n1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8('\n')));
        uint64_t n2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8('\n')));
        uint64_t n3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8('\n')));
        
        uint64_t q0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8('"')));
        uint64_t q1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8('"')));
        uint64_t q2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8('"')));
        uint64_t q3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8('"')));

        uint64_t newlines = n0 | (n1 << 16) | (n2 << 32) | (n3 << 48);
        uint64_t quotes   = q0 | (q1 << 16) | (q2 << 32) | (q3 << 48);

        // Prefix XOR to calculate "inside string" mask for this block
        uint64_t internal_mask = quotes;
        internal_mask ^= (internal_mask << 1);
        internal_mask ^= (internal_mask << 2);
        internal_mask ^= (internal_mask << 4);
        internal_mask ^= (internal_mask << 8);
        internal_mask ^= (internal_mask << 16);
        internal_mask ^= (internal_mask << 32);

        // Combine with global state
        uint64_t final_inside_mask = internal_mask ^ quote_state;

        // Count valid newlines (newlines that are NOT inside mask)
        total_count += __builtin_popcountll(newlines & ~final_inside_mask);

        // Update global state: flip if we saw an odd number of quotes
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
    static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
    uint64_t total_newlines = 0;
    int inside_quote = 0;
    size_t bytes_read;

    // Use stdin
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, stdin)) > 0) {
        total_newlines += process_buffer(buffer, bytes_read, &inside_quote);
    }

    printf("%llu\n", total_newlines);
    return 0;
}
