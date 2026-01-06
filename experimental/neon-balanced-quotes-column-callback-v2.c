// gcc -O3 -march=native -o neon-balanced-quotes-column-callback-v2 neon-balanced-quotes-column-callback-v2.c -lzsvutil

/*
// Example Callback1
void on_record1(void *ctx_void) {
process_ctx_t *ctx = (process_ctx_t*)ctx_void;
size_t count = column_count(ctx);
size_t row_num = row_count(ctx);

printf("Row %zu, Cols: %zu\n", row_num, count);
// Print first 3 columns to verify
for(size_t i = 0; i < count; i++) {
column_t c = get_column(ctx, i);
printf("Row %zu, Col %zu: '%.*s'\n", row_num, i + 1, (int)c.len, c.str);
}
printf("----\n");
}
*/


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <arm_neon.h>

#include <zsv/utils/writer.h>

#define BUF_SIZE (256 * 1024 )
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
  // Count commas belonging to current row
  // Start searching from current_comma_idx
  size_t count = 0;
  size_t idx = ctx->current_comma_idx;
  size_t limit = ctx->row_end_offset;
    
  // Scan forward in the pre-computed array
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
    
  // Identify boundaries from col_offsets array
  // The commas for this row start at ctx->current_comma_idx
    
  size_t actual_idx = ctx->current_comma_idx + index;
    
  // Bounds check: 
  // 1. Is the requested index beyond the commas available for this row?
  // 2. Or is the underlying array index out of bounds?
  // Note: We need to know how many commas are in THIS row to check index validity strictly,
  // but simplified check is: if the comma at actual_idx is beyond row_end, it's invalid (unless it's the last column).
    
  // Start Position
  size_t start;
  if (index == 0) {
    start = ctx->row_start_offset;
  } else {
    // Look at previous comma
    size_t prev_comma_idx = actual_idx - 1;
    if (prev_comma_idx >= ctx->total_col_offsets || ctx->col_offsets[prev_comma_idx] >= ctx->row_end_offset) {
      return col; // Index out of bounds
    }
    start = ctx->col_offsets[prev_comma_idx] + 1;
  }

  // End Position
  size_t end;
  // Check if this is the last column (no comma after it)
  if (actual_idx < ctx->total_col_offsets && ctx->col_offsets[actual_idx] < ctx->row_end_offset) {
    end = ctx->col_offsets[actual_idx];
  } else {
    end = ctx->row_end_offset;
  }
    
  col.str = (const char *)(ctx->current_buffer + start);
  col.len = (end >= start) ? (end - start) : 0;
  return col;
}

// --- Main Newline Processor ---

void process_chunk(const uint8_t *data, size_t len, process_ctx_t *ctx) {
  // 1. Reset State
  // We scan from 0, so we rebuild the comma list from scratch.
  ctx->total_col_offsets = 0;
  ctx->current_comma_idx = 0; 
  ctx->last_newline_idx = -1;
  ctx->current_buffer = data; // Update buffer pointer for get_column

  // 2. Local Caches
  uint64_t current_total_rows = ctx->total_rows;
  int current_inside_quote = 0; // Always 0 at start of buffer (post-newline)
  newline_callback_t cb = ctx->on_record;
  
  // If we have no callback, we don't need to store commas.
  bool scan_cols = (cb != NULL);

  size_t i = 0;
  for (; i + 64 <= len; i += 64) {
    // A. Load Data
    uint8x16_t b0 = vld1q_u8(data + i);
    uint8x16_t b1 = vld1q_u8(data + i + 16);
    uint8x16_t b2 = vld1q_u8(data + i + 32);
    uint8x16_t b3 = vld1q_u8(data + i + 48);

    // B. Detect Characters
    uint64_t q0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8('"')));
    uint64_t q1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8('"')));
    uint64_t q2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8('"')));
    uint64_t q3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8('"')));
    uint64_t quotes = q0 | (q1 << 16) | (q2 << 32) | (q3 << 48);

    uint64_t n0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8('\n')));
    uint64_t n1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8('\n')));
    uint64_t n2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8('\n')));
    uint64_t n3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8('\n')));
    uint64_t newlines = n0 | (n1 << 16) | (n2 << 32) | (n3 << 48);

    // C. Calculate Quote Mask (B) - ONE TIME
    uint64_t B = quotes;
    B = B ^ (B << 1);
    B = B ^ (B << 2);
    B = B ^ (B << 4);
    B = B ^ (B << 8);
    B = B ^ (B << 16);
    B = B ^ (B << 32);
    if (current_inside_quote) B = ~B;

    // D. Process Commas (Only if needed)
    // We populate commas BEFORE processing newlines so the callback sees them.
    if (scan_cols) {
        uint64_t c0 = neon_movemask(vceqq_u8(b0, vdupq_n_u8(',')));
        uint64_t c1 = neon_movemask(vceqq_u8(b1, vdupq_n_u8(',')));
        uint64_t c2 = neon_movemask(vceqq_u8(b2, vdupq_n_u8(',')));
        uint64_t c3 = neon_movemask(vceqq_u8(b3, vdupq_n_u8(',')));
        uint64_t commas = c0 | (c1 << 16) | (c2 << 32) | (c3 << 48);
        
        uint64_t valid_commas = commas & ~B;
        while (valid_commas) {
            int bit_idx = __builtin_ctzll(valid_commas);
            if (ctx->total_col_offsets >= ctx->col_capacity) {
                ctx->col_capacity *= 2;
                ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
            }
            ctx->col_offsets[ctx->total_col_offsets++] = i + bit_idx;
            valid_commas &= (valid_commas - 1);
        }
    }

    // E. Process Newlines
    uint64_t valid_newlines = newlines & ~B;
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
          
          cb(ctx); // Callback now sees all commas from this block
          
          ctx->row_start_offset = abs_offset + 1;
          
          // Fast-forward comma cursor
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

    current_inside_quote = (B >> 63) & 1;
  }

  // F. Tail Handling
  for (; i < len; i++) {
    uint8_t c = data[i];
    if (c == '"') {
      current_inside_quote = !current_inside_quote;
    } else if (!current_inside_quote) {
      if (c == ',' && scan_cols) {
         if (ctx->total_col_offsets >= ctx->col_capacity) {
            ctx->col_capacity *= 2;
            ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
         }
         ctx->col_offsets[ctx->total_col_offsets++] = i;
      } else if (c == '\n') {
         current_total_rows++;
         ctx->last_newline_idx = i;
         if (cb) {
            ctx->row_end_offset = i;
            ctx->total_rows = current_total_rows;
            cb(ctx);
            ctx->row_start_offset = i + 1;
            while (ctx->current_comma_idx < ctx->total_col_offsets && 
                   ctx->col_offsets[ctx->current_comma_idx] < ctx->row_start_offset) {
               ctx->current_comma_idx++;
            }
         }
      }
    }
  }
  
  ctx->total_rows = current_total_rows;
  // We don't save ctx->inside_quote because we restart from 0 next time.
}

// Example Callback
void on_record(void *ctx_void) {
  process_ctx_t *ctx = (process_ctx_t*)ctx_void;
  size_t count = column_count(ctx);
  size_t row_num = row_count(ctx);
  // printf("Row %zu, Cols: %zu\n", row_num, count);
}

static void on_record2(void *ctx_void) {
  process_ctx_t *ctx = (process_ctx_t*)ctx_void;
  size_t count = column_count(ctx);
  size_t row_num = row_count(ctx);

  const size_t out_cols[] = { 0, 1, 2, 5, 4 };
  const size_t out_colcount = 5;
  for(size_t i = 0; i < out_colcount; i++) {
    column_t c = get_column(ctx, out_cols[i]);
    //    if(c.len > 100)
       zsv_writer_cell(ctx->writer, i == 0, (const unsigned char *)c.str, c.len, 0);
  }
}

int main() {
  static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
  process_ctx_t ctx = {
    .total_rows = 0,
    .inside_quote = 0,
    .last_newline_idx = -1,
        
    // Pass NULL for max speed (no columns), or on_record to test splitting
    //    .on_record = on_record2,
    .on_record = NULL,
        
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
        
    process_chunk(buffer, total_in_buffer, &ctx);
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
  printf("%llu\n", ctx.total_rows);
  return 0;
}
