// gcc -O3 -march=native -o count-neon-callback count-neon-callback.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arm_neon.h>

#define BUF_SIZE (256 * 1024)

typedef void (*newline_callback_t)(size_t offset, void *user_data);

typedef struct {
    uint64_t total_count;       // Global count of valid newlines
    int inside_quote;           // State at the end of the currently processed data
    int last_char_was_newline;  // State at the end of the currently processed data
    ssize_t last_newline_idx;   // Location of the last VALID newline (relative to buffer start), or -1
} process_ctx_t;

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

void process_buffer(const uint8_t *data, size_t len, size_t start_offset, process_ctx_t *ctx, newline_callback_t cb, void *cb_user_data) {
    size_t i = start_offset;
    
    // Reset snapshot index for this new chunk.
    // We only want to track valid newlines found in THIS specific call 
    // (or keep the previous one if it was in the preserved tail, 
    // but the logic in main handles the reset of valid_bytes, so -1 is safe here).
    ctx->last_newline_idx = -1;

    // Load local state variables
    uint64_t current_total_count = ctx->total_count;
    int current_inside_quote = ctx->inside_quote; 
    int last_char_was_newline = ctx->last_char_was_newline;

    // SIMD Loop
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

        // --- STATE LOGIC ---
        uint64_t nl_shifted = (newlines << 1) | (last_char_was_newline ? 1ULL : 0ULL);
        uint64_t valid_openers = quotes & nl_shifted;
        uint64_t invalid_openers = quotes & ~nl_shifted;

        uint64_t A = ~invalid_openers; 
        uint64_t B = valid_openers;    

        B = B ^ (A & (B << 1));  A = A & (A << 1);
        B = B ^ (A & (B << 2));  A = A & (A << 2);
        B = B ^ (A & (B << 4));  A = A & (A << 4);
        B = B ^ (A & (B << 8));  A = A & (A << 8);
        B = B ^ (A & (B << 16)); A = A & (A << 16);
        B = B ^ (A & (B << 32)); A = A & (A << 32);

        uint64_t state_mask = (current_inside_quote ? A : 0) ^ B;
        
        // This mask contains only VALID newlines (not inside quotes)
        uint64_t valid_newlines_mask = newlines & ~state_mask;

        // --- UPDATE SNAPSHOT LOCATION ---
        // We only care about splitting the buffer at a VALID record separator.
        if (valid_newlines_mask) {
            int last_bit = 63 - __builtin_clzll(valid_newlines_mask);
            ctx->last_newline_idx = i + last_bit;
        }

        // --- ACCUMULATE ---
        if (cb) {
            uint64_t temp_mask = valid_newlines_mask;
            while (temp_mask) {
                int bit_idx = __builtin_ctzll(temp_mask);
                current_total_count++;
                cb(i + bit_idx, cb_user_data);
                temp_mask &= (temp_mask - 1);
            }
        } else
          current_total_count += __builtin_popcountll(valid_newlines_mask);

        current_inside_quote = (state_mask >> 63) & 1;
        last_char_was_newline = (newlines >> 63) & 1;
    }

    ctx->total_count = current_total_count;
    ctx->inside_quote = current_inside_quote;
    ctx->last_char_was_newline = last_char_was_newline;

    // Tail handling
    for (; i < len; i++) {
        bool is_quote = (data[i] == '"');
        bool is_newline = (data[i] == '\n');

        if (is_quote) {
            if (ctx->inside_quote) {
                ctx->inside_quote = 0;
            } else {
                if (ctx->last_char_was_newline) {
                    ctx->inside_quote = 1;
                }
            }
        } else if (is_newline) {
            if (!ctx->inside_quote) {
                ctx->total_count++;
                if (cb) cb(i, cb_user_data);
                ctx->last_newline_idx = i;
            }
        }
        
        ctx->last_char_was_newline = is_newline;
    }
}

int main() {
    static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
    
    process_ctx_t ctx = {
        .total_count = 0,
        .inside_quote = 0,
        .last_char_was_newline = 1,
        .last_newline_idx = -1
    };
    
    size_t valid_bytes = 0;

    while (1) {
        size_t space_available = BUF_SIZE - valid_bytes;
        
        bool force_flush = (space_available == 0);
        
        size_t bytes_read = 0;
        if (!force_flush) {
            bytes_read = fread(buffer + valid_bytes, 1, space_available, stdin);
            if (bytes_read == 0 && valid_bytes == 0) break; 
        }

        size_t total_in_buffer = valid_bytes + bytes_read;
        
        process_buffer(buffer, total_in_buffer, valid_bytes, &ctx, NULL, NULL);

        if (ctx.last_newline_idx != -1) {
            // A VALID newline was found.
            size_t consumed_len = ctx.last_newline_idx + 1;
            size_t remaining = total_in_buffer - consumed_len;
            
            if (remaining > 0) {
                memmove(buffer, buffer + consumed_len, remaining);
            }
            valid_bytes = remaining;
        } else {
            // No valid newline found in this new chunk.
            if (force_flush || (bytes_read == 0 && valid_bytes > 0)) {
                // Buffer full or EOF: Discard buffer, state is preserved in ctx.
                valid_bytes = 0;
            } else {
                // Keep accumulating
                valid_bytes = total_in_buffer;
            }
        }
        
        if (bytes_read == 0 && valid_bytes == 0) break;
    }

    printf("%llu\n", ctx.total_count);
    return 0;
}
