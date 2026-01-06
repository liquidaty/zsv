// gcc -O3 -march=native -o count-neon count-neon.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <arm_neon.h>

#define BUF_SIZE (256 * 1024)

inline uint16_t neon_movemask(uint8x16_t input) {
    const uint8x16_t bit_weights = {
        1, 2, 4, 8, 16, 32, 64, 128, 
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t masked = vandq_u8(input, bit_weights);
    uint8x8_t low = vget_low_u8(masked);
    uint8x8_t high = vget_high_u8(masked);
    return vaddv_u8(low) | (vaddv_u8(high) << 8);
}

// Added parameter: last_char_was_newline_ref
uint64_t process_buffer(const uint8_t *data, size_t len, int *inside_quote_global, int *last_char_was_newline_ref) {
    uint64_t total_count = 0;
    size_t i = 0;
    
    // Global state: 1 if inside, 0 if outside
    uint64_t current_state = *inside_quote_global ? 1 : 0;

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

        // --- NEW LOGIC START ---

        // 1. Identify "Valid Openers" (O): Quotes preceded by a newline (or start of file)
        uint64_t nl_shifted = (newlines << 1) | (*last_char_was_newline_ref ? 1ULL : 0ULL);
        uint64_t valid_openers = quotes & nl_shifted;

        // 2. Identify "Invalid Openers" (I): Quotes NOT preceded by a newline.
        //    These act as "Resets" (Force Close) if we are inside a string.
        uint64_t invalid_openers = quotes & ~nl_shifted;

        // 3. Parallel Prefix Scan for State Machine
        //    State Update Rule: S_next = (S_curr & ~Invalid) ^ Valid
        //    We define two variables for the prefix scan:
        //    A (Alive/Pass-through): Mask of regions that have NOT been reset (Force Closed).
        //    B (Base State): Cumulative toggles generated internally, subject to resets.
        
        uint64_t A = ~invalid_openers; // 0 at Reset positions, 1 otherwise
        uint64_t B = valid_openers;    // 1 at Toggle positions

        // Perform parallel prefix scan (6 steps for 64 bits)
        // Composition rule: (A2, B2) after (A1, B1) -> (A1 & A2, (B1 & A2) ^ B2)
        // We accumulate from LSB to MSB.
        
        // Step 1
        B = B ^ (A & (B << 1));
        A = A & (A << 1);
        // Step 2
        B = B ^ (A & (B << 2));
        A = A & (A << 2);
        // Step 3
        B = B ^ (A & (B << 4));
        A = A & (A << 4);
        // Step 4
        B = B ^ (A & (B << 8));
        A = A & (A << 8);
        // Step 5
        B = B ^ (A & (B << 16));
        A = A & (A << 16);
        // Step 6
        B = B ^ (A & (B << 32));
        A = A & (A << 32);

        // Calculate final state mask for this block
        // If 'current_state' (global) survives the 'A' filter (no resets), we XOR it with B.
        uint64_t state_mask = (current_state ? A : 0) ^ B;

        // Count valid newlines (newlines where state_mask is 0)
        total_count += __builtin_popcountll(newlines & ~state_mask);

        // Update global state and newline history for next block
        current_state = (state_mask >> 63) & 1;
        *last_char_was_newline_ref = (newlines >> 63) & 1;

        // --- NEW LOGIC END ---
    }

    *inside_quote_global = current_state;

    // Tail handling (Scalar fallback logic)
    for (; i < len; i++) {
        bool is_quote = (data[i] == '"');
        bool is_newline = (data[i] == '\n');

        if (is_quote) {
            if (*inside_quote_global) {
                // If inside, ANY quote closes it.
                *inside_quote_global = 0;
            } else {
                // If outside, only valid quotes open it.
                if (*last_char_was_newline_ref) {
                    *inside_quote_global = 1;
                }
            }
        } else if (is_newline && !(*inside_quote_global)) {
            total_count++;
        }
        
        *last_char_was_newline_ref = is_newline;
    }

    return total_count;
}

int main() {
    static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
    uint64_t total_newlines = 0;
    int inside_quote = 0;
    
    // Initialize to 1: File start is treated as if preceded by newline
    int last_char_was_newline = 1; 
    
    size_t bytes_read;

    // Use stdin
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, stdin)) > 0) {
        last_c = buffer[bytes_read-1];
        total_newlines += process_buffer(buffer, bytes_read, &inside_quote, &last_char_was_newline);
    }

    printf("%llu\n", total_newlines);
    return 0;
}