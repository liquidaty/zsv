// gcc -O3 -march=native -o count-neon-columns-callback-v2 count-neon-columns-callback-v2.c -lzsvutil

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <arm_neon.h>

#include <zsv/utils/writer.h>

#define BUF_SIZE (256 * 1024)
#define INITIAL_COL_CAPACITY 1024

typedef struct {
  const char *str;
  size_t len;
} column_t;

// Forward declaration
struct process_ctx_t;
typedef void (*newline_callback_t)(void *ctx);

typedef struct {
  // --- Global State ---
  uint64_t total_rows;        
  int inside_quote;           // State carried over between buffers
  int last_char_was_newline;  // State carried over between buffers
  ssize_t last_newline_idx;   // Used for buffer sliding

  // --- Row State ---
  const uint8_t *current_buffer; 
  size_t row_start_offset;       
  size_t row_end_offset;         
    
  // --- Column State ---
  uint64_t *col_offsets;      // Array of ALL valid comma offsets in current buffer
  size_t total_col_offsets;   // Number of commas found in current buffer
  size_t col_capacity;        
  size_t current_comma_idx;   // Cursor into col_offsets for the current row

  // --- Callback ---
  newline_callback_t on_record;

  // csv writer
  zsv_csv_writer writer;
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

// --- Column API ---

size_t column_count(process_ctx_t *ctx) {
  size_t count = 0;
  size_t idx = ctx->current_comma_idx;
  size_t limit = ctx->row_end_offset;
    
  while (idx < ctx->total_col_offsets && ctx->col_offsets[idx] < limit) {
    count++;
    idx++;
  }
  return count + 1;
}

size_t row_count(process_ctx_t *ctx) {
  return ctx->total_rows;
}

column_t get_column(process_ctx_t *ctx, size_t index) {
  column_t col = {0, 0};
  size_t actual_idx = ctx->current_comma_idx + index;
    
  size_t start;
  if (index == 0) {
    start = ctx->row_start_offset;
  } else {
    size_t prev_comma_idx = actual_idx - 1;
    if (prev_comma_idx >= ctx->total_col_offsets || ctx->col_offsets[prev_comma_idx] >= ctx->row_end_offset) {
      return col; 
    }
    start = ctx->col_offsets[prev_comma_idx] + 1;
  }

  size_t end;
  if (actual_idx < ctx->total_col_offsets && ctx->col_offsets[actual_idx] < ctx->row_end_offset) {
    end = ctx->col_offsets[actual_idx];
  } else {
    end = ctx->row_end_offset;
  }
    
  col.str = (const char *)(ctx->current_buffer + start);
  col.len = (end >= start) ? (end - start) : 0;
  return col;
}

// --- Combined Processor ---
// Consolidates scan_commas and process_newlines into a single pass.
// Only pays the cost of comma scanning instructions if ctx->on_record is set.
void process_buffer(const uint8_t *data, size_t len, process_ctx_t *ctx) {
  size_t i = 0;
    
  ctx->current_buffer = data;
  ctx->last_newline_idx = -1;
  ctx->total_col_offsets = 0; // Reset for new buffer
  ctx->current_comma_idx = 0; // Reset cursor

  // Cache local state
  uint64_t current_total_rows = ctx->total_rows;
  int current_inside_quote = ctx->inside_quote; 
  int last_char_was_newline = ctx->last_char_was_newline;
  newline_callback_t cb = ctx->on_record;

  for (; i + 64 <= len; i += 64) {
    uint8x16_t b0 = vld1q_u8(data + i);
    uint8x16_t b1 = vld1q_u8(data + i + 16);
    uint8x16_t b2 = vld1q_u8(data + i + 32);
    uint8x16_t b3 = vld1q_u8(data + i + 48);

    // 1. Calculate Quotes & Newlines (Always needed)
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

    // 2. State Machine Logic (Quote Masking)
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

    // state_mask: 1 where we are inside quotes
    uint64_t state_mask = (current_inside_quote ? A : 0) ^ B;

    // 3. Conditional Comma Processing
    // OPTIMIZATION: Only execute SIMD comparisons and writes if callback exists
    if (cb) {
        uint64_t c0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8(',')));
        uint64_t c1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8(',')));
        uint64_t c2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8(',')));
        uint64_t c3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8(',')));
        uint64_t commas = c0 | (c1 << 16) | (c2 << 32) | (c3 << 48);

        uint64_t valid_commas = commas & ~state_mask;

        if (valid_commas) {
            // Check capacity once per chunk roughly
            if (ctx->total_col_offsets + 64 >= ctx->col_capacity) {
                ctx->col_capacity *= 2;
                ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
            }
            
            while (valid_commas) {
                int bit_idx = __builtin_ctzll(valid_commas);
                ctx->col_offsets[ctx->total_col_offsets++] = i + bit_idx;
                valid_commas &= (valid_commas - 1);
            }
        }
    }

    // 4. Newline Processing
    uint64_t valid_newlines = newlines & ~state_mask;

    if (valid_newlines) {
      int last_bit = 63 - __builtin_clzll(valid_newlines);
      ctx->last_newline_idx = i + last_bit;

      if (cb) {
        uint64_t events = valid_newlines;
        while (events) {
          int bit_idx = __builtin_ctzll(events);
          size_t abs_offset = i + bit_idx;
                    
          current_total_rows++;
          
          ctx->total_rows = current_total_rows;
          ctx->row_end_offset = abs_offset;
                    
          cb(ctx);
                    
          ctx->row_start_offset = abs_offset + 1;
                    
          // Sync comma cursor for next row
          while (ctx->current_comma_idx < ctx->total_col_offsets && 
                 ctx->col_offsets[ctx->current_comma_idx] < ctx->row_start_offset) {
            ctx->current_comma_idx++;
          }
                    
          events &= (events - 1);
        }
      } else {
        current_total_rows += __builtin_popcountll(valid_newlines);
      }
    }

    current_inside_quote = (state_mask >> 63) & 1;
    last_char_was_newline = (newlines >> 63) & 1;
  }

  ctx->total_rows = current_total_rows;
  ctx->inside_quote = current_inside_quote;
  ctx->last_char_was_newline = last_char_was_newline;

  // Tail handling (Scalar)
  for (; i < len; i++) {
    bool is_quote = (data[i] == '"');
    bool is_newline = (data[i] == '\n');
    bool is_comma = (data[i] == ',');

    if (is_quote) {
      if (ctx->inside_quote) {
        ctx->inside_quote = 0;
      } else {
        if (ctx->last_char_was_newline) {
          ctx->inside_quote = 1;
        }
      }
    } else if (!ctx->inside_quote) {
        // Handle Commas (Conditioned on callback)
        if (cb && is_comma) {
            if (ctx->total_col_offsets >= ctx->col_capacity) {
                ctx->col_capacity *= 2;
                ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
            }
            ctx->col_offsets[ctx->total_col_offsets++] = i;
        }

        // Handle Newlines
        if (is_newline) {
            ctx->total_rows++;
            ctx->last_newline_idx = i;
            
            if (cb) {
                ctx->row_end_offset = i;
                cb(ctx);
                ctx->row_start_offset = i + 1;
                while (ctx->current_comma_idx < ctx->total_col_offsets && 
                    ctx->col_offsets[ctx->current_comma_idx] < ctx->row_start_offset) {
                ctx->current_comma_idx++;
                }
            }
        }
    }
        
    ctx->last_char_was_newline = is_newline;
  }
}

static void on_record2(void *ctx_void) {
  process_ctx_t *ctx = (process_ctx_t*)ctx_void;
  size_t count = column_count(ctx);
  // size_t row_num = row_count(ctx);

  const size_t out_cols[] = { 0, 1, 2, 5, 4 };
  const size_t out_colcount = 5;
  for(size_t i = 0; i < out_colcount; i++) {
    column_t c = get_column(ctx, out_cols[i]);
    zsv_writer_cell(ctx->writer, i == 0, (const unsigned char *)c.str, c.len, 0);
  }
}

int main() {
  static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
  process_ctx_t ctx = {
    .total_rows = 0,
    .inside_quote = 0,
    .last_char_was_newline = 1,
    .last_newline_idx = -1,
        
    // Pass NULL for max speed (no columns), or on_record2
    .on_record = on_record2, 
        
    .col_capacity = INITIAL_COL_CAPACITY,
    .total_col_offsets = 0,
    .current_comma_idx = 0,
    .row_start_offset = 0,
    .writer = NULL
  };

  struct zsv_csv_writer_options writer_opts = {0};
  writer_opts.stream = stdout;
  ctx.writer = zsv_writer_new(&writer_opts);
    
  ctx.col_offsets = malloc(INITIAL_COL_CAPACITY * sizeof(uint64_t));

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
    
    // Process entire buffer in one optimized pass
    process_buffer(buffer, total_in_buffer, &ctx);

    if (ctx.last_newline_idx != -1) {
      size_t consumed_len = ctx.last_newline_idx + 1;
      size_t remaining = total_in_buffer - consumed_len;
            
      if (remaining > 0) {
        memmove(buffer, buffer + consumed_len, remaining);
      }
      valid_bytes = remaining;
      ctx.row_start_offset = 0; 
    } else {
      if (force_flush || (bytes_read == 0 && valid_bytes > 0)) {
        // Buffer full of garbage or EOF with partial line
        valid_bytes = 0;
        ctx.row_start_offset = 0;
      } else {
        valid_bytes = total_in_buffer;
      }
    }
        
    if (bytes_read == 0 && valid_bytes == 0) break;
  }

  free(ctx.col_offsets);
  zsv_writer_delete(ctx.writer);
  fprintf(stderr, "%llu\n", ctx.total_rows);
  return 0;
}
