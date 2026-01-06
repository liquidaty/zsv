// to compile:
// gcc -DTEST_CELL_COUNT -O3 -march=native -o cell-count-balanced-quotes-callback-neon cell-count-balanced-quotes-callback-neon.c

#ifndef CELL_COUNT_EXPERIMENTAL
#define CELL_COUNT_EXPERIMENTAL

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arm_neon.h>

#define BUF_SIZE (256 * 1024)

typedef void (*parser_callback_cell_t)(void *context, const uint8_t *start, size_t len);
typedef void (*parser_callback_row_t)(void *context);

struct counts {
    size_t cell;
    size_t row;
};

static inline uint16_t neon_movemask(uint8x16_t input) {
    const uint8x16_t bit_weights = {
        1, 2, 4, 8, 16, 32, 64, 128, 
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t masked = vandq_u8(input, bit_weights);
    uint8x8_t low = vget_low_u8(masked);
    uint8x8_t high = vget_high_u8(masked);
    return vaddv_u8(low) | (vaddv_u8(high) << 8);
}

static void _process_buffer_core(
    const uint8_t *data, 
    size_t len, 
    int *inside_quote_global, 
    void *ctx, 
    parser_callback_row_t cb_row,
    parser_callback_cell_t cb_cell,
    const char cell_delimiter // ignored if enable_cell_parsing is false
) {
  const bool enable_cell_parsing = cb_cell ? true : false;
    size_t i = 0;
    uint64_t quote_state = *inside_quote_global ? ~0ULL : 0ULL;
    const uint8_t *current_field_start = data;

    uint8x16_t v_newline = vdupq_n_u8('\n');
    uint8x16_t v_quote   = vdupq_n_u8('"');
    
    // cell delimiter
    uint8x16_t v_cell;
    if (enable_cell_parsing) {
        v_cell = vdupq_n_u8(cell_delimiter);
    }
    for (; i + 64 <= len; i += 64) {
        // 1. Load Data
        uint8x16_t b0 = vld1q_u8(data + i);
        uint8x16_t b1 = vld1q_u8(data + i + 16);
        uint8x16_t b2 = vld1q_u8(data + i + 32);
        uint8x16_t b3 = vld1q_u8(data + i + 48);

        // 2. Identify Targets
        uint8x16_t t0_vec, t1_vec, t2_vec, t3_vec;

        if (enable_cell_parsing) {
            // Mode A: Search for Newline OR Cell
            t0_vec = vorrq_u8(vceqq_u8(b0, v_newline), vceqq_u8(b0, v_cell));
            t1_vec = vorrq_u8(vceqq_u8(b1, v_newline), vceqq_u8(b1, v_cell));
            t2_vec = vorrq_u8(vceqq_u8(b2, v_newline), vceqq_u8(b2, v_cell));
            t3_vec = vorrq_u8(vceqq_u8(b3, v_newline), vceqq_u8(b3, v_cell));
        } else {
            // Mode B: Search for Newline Only (Faster! No OR instruction)
            t0_vec = vceqq_u8(b0, v_newline);
            t1_vec = vceqq_u8(b1, v_newline);
            t2_vec = vceqq_u8(b2, v_newline);
            t3_vec = vceqq_u8(b3, v_newline);
        }

        uint64_t t0 = neon_movemask(t0_vec);
        uint64_t t1 = neon_movemask(t1_vec);
        uint64_t t2 = neon_movemask(t2_vec);
        uint64_t t3 = neon_movemask(t3_vec);
        
        uint64_t q0 = neon_movemask(vceqq_u8(b0, v_quote));
        uint64_t q1 = neon_movemask(vceqq_u8(b1, v_quote));
        uint64_t q2 = neon_movemask(vceqq_u8(b2, v_quote));
        uint64_t q3 = neon_movemask(vceqq_u8(b3, v_quote));

        // 3. Bitmasks
        uint64_t targets = t0 | (t1 << 16) | (t2 << 32) | (t3 << 48);
        uint64_t quotes  = q0 | (q1 << 16) | (q2 << 32) | (q3 << 48);

        // 4. Prefix XOR (Quote Logic)
        uint64_t internal_mask = quotes;
        internal_mask ^= (internal_mask << 1);
        internal_mask ^= (internal_mask << 2);
        internal_mask ^= (internal_mask << 4);
        internal_mask ^= (internal_mask << 8);
        internal_mask ^= (internal_mask << 16);
        internal_mask ^= (internal_mask << 32);

        uint64_t final_inside_mask = internal_mask ^ quote_state;
        uint64_t valid_mask = targets & ~final_inside_mask;

        // 5. Iterate bits
        while (valid_mask != 0) {
            int bit_pos = __builtin_ctzll(valid_mask);
            const uint8_t *delim_ptr = data + i + bit_pos;
            size_t len = delim_ptr - current_field_start;

            if (enable_cell_parsing) {
                if (*delim_ptr == '\n') {
                    cb_cell(ctx, current_field_start, len);
                    cb_row(ctx);
                } else {
                    cb_cell(ctx, current_field_start, len);
                }
            } else { // we only iterate on rows
              cb_row(ctx);
            }

            current_field_start = delim_ptr + 1;
            valid_mask &= (valid_mask - 1);
        }

        if (__builtin_popcountll(quotes) & 1) {
            quote_state = ~quote_state;
        }
    }

    *inside_quote_global = (quote_state != 0);

    // Tail handling
    for (; i < len; i++) {
        if (data[i] == '"') {
            *inside_quote_global = !(*inside_quote_global);
        }
        else if (!(*inside_quote_global)) {
            if (data[i] == '\n') {
              if(enable_cell_parsing)
                cb_cell(ctx, current_field_start, len);
              cb_row(ctx);
              current_field_start = data + i + 1;
            } 
            else if (enable_cell_parsing && data[i] == cell_delimiter) {
                cb_cell(ctx, current_field_start, (data + i) - current_field_start);
                current_field_start = data + i + 1;
            }
        }
    }
}

// --- PUBLIC API ---

// Public wrapper that dispatches to the correct optimized kernel
static inline void process_buffer(
    const uint8_t *data, 
    size_t len, 
    int *inside_quote_global, 
    void *ctx, 
    parser_callback_row_t cb_row,
    parser_callback_cell_t cb_cell, // Can be NULL if enable_cell is false
    bool enable_cell,
    char cell_delimiter
) {
    if (enable_cell) {
        // Generates code with Vector ORs and Branching callbacks
        _process_buffer_core(data, len, inside_quote_global, ctx, cb_row, cb_cell, cell_delimiter);
    } else {
        // Generates code with NO Vector ORs and Unconditional callbacks
        _process_buffer_core(data, len, inside_quote_global, ctx, cb_row, NULL, 0);
    }
}

static void count_row(void *ctx) {
  struct counts *counts = (struct counts *)ctx;
  counts->row++;
}

static void count_cell(void *ctx, const uint8_t *start, size_t len) {
  struct counts *counts = (struct counts *)ctx;
  counts->cell++;
}

#ifdef TEST_CELL_COUNT
int main(int argc, const char *argv[]) {
    static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
    struct counts counts = { 0 };
    int inside_quote = 0;
    size_t bytes_read;
    int do_count_cells = 0;

    FILE *fin = NULL;
    for(int i = 1; i < argc; i++) {
      if(!strcmp(argv[i], "--count-cells"))
        do_count_cells = 1;
      else if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
        printf("%s [--count-cells] [--help,-h]\n", argv[0]);
        return 0;
      } else if(!fin) {
        fin = fopen(argv[i], "rb");
        if(!fin) {
          perror(argv[i]);
          return 1;
        }
      } else {
        fprintf(stderr, "Unrecognized argument %s\n", argv[i]);
        return 1;
      }
    }

    // We use the same buffer pointer logic.
    // Note: For a real parser, you would need logic here to handle fields 
    // that get cut off at the end of the buffer (using memcpy to move the 
    // partial field to the start of the next buffer).
    if(!fin)
      fin = stdin;
    while ((bytes_read = fread(buffer, 1, BUF_SIZE, fin)) > 0) {
      process_buffer(buffer, bytes_read, &inside_quote, &counts, count_row, count_cell, do_count_cells, ',');
    }

    printf("Rows: %zu, cells: %zu\n", counts.row, counts.cell);
    if(fin != stdin)
      fclose(fin);
    return 0;
}

#endif
#endif
