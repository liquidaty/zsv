// gcc -O3 -march=native -o count-neon-columns-callback-v3 count-neon-columns-callback-v3.c -lzsvutil
#ifndef CELL_COUNT_EXPERIMENTAL
#define CELL_COUNT_EXPERIMENTAL

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <arm_neon.h>

#include <zsv/utils/writer.h>

#define BUF_SIZE (256 * 1024)
#define INITIAL_COL_CAPACITY 1024
#define COL_QUOTED_FLAG (1ULL << 63)
#define COL_OFFSET_MASK (~COL_QUOTED_FLAG)

typedef struct {
  const char *str;
  size_t len;
  bool quoted; // New flag indicating if the column contained double-quotes
} column_t;

// Forward declaration
struct process_ctx_t;
typedef void (*newline_callback_t)(void *ctx);

typedef struct {
  // --- Global State ---
  uint64_t total_rows;
  int inside_quote;           // State carried over between buffers
  int last_char_was_newline;  // State carried over between buffers
  int last_char_was_cr;       // State carried over between buffers
  ssize_t last_newline_idx;   // Used for buffer sliding

  // --- Quote Tracking State ---
  uint64_t accumulated_quotes; // Tracks if quotes were seen in the current column segment
  bool last_col_quoted;        // Stores quote status for the EOL column (handled separately)

  // --- Row State ---
  const uint8_t *current_buffer;
  size_t row_start_offset;
  size_t row_end_offset;

  // --- Column State ---
  uint64_t *col_offsets;      // Array of comma offsets + Quote Flag in MSB
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

  while (idx < ctx->total_col_offsets && (ctx->col_offsets[idx] & COL_OFFSET_MASK) < limit) {
    count++;
    idx++;
  }
  return count + 1;
}

size_t row_count(process_ctx_t *ctx) {
  return ctx->total_rows;
}

column_t get_column(process_ctx_t *ctx, size_t index) {
  column_t col = {0, 0, false};
  size_t actual_idx = ctx->current_comma_idx + index;

  size_t start;
  if (index == 0) {
    start = ctx->row_start_offset;
  } else {
    size_t prev_comma_idx = actual_idx - 1;
    if (prev_comma_idx >= ctx->total_col_offsets || (ctx->col_offsets[prev_comma_idx] & COL_OFFSET_MASK) >= ctx->row_end_offset) {
      return col;
    }
    start = (ctx->col_offsets[prev_comma_idx] & COL_OFFSET_MASK) + 1;
  }

  size_t end;
  bool quoted_status = false;

  // Check if the requested index points to a comma within the current row
  if (actual_idx < ctx->total_col_offsets && (ctx->col_offsets[actual_idx] & COL_OFFSET_MASK) < ctx->row_end_offset) {
    uint64_t raw_offset = ctx->col_offsets[actual_idx];
    end = raw_offset & COL_OFFSET_MASK;
    // The flag stored at a comma tells us if the column *ending* at that comma was quoted
    quoted_status = (raw_offset & COL_QUOTED_FLAG) != 0;
  } else {
    // This is the last column, ending at the newline/row_end
    end = ctx->row_end_offset;
    quoted_status = ctx->last_col_quoted;
  }

  col.str = (const char *)(ctx->current_buffer + start);
  col.len = (end >= start) ? (end - start) : 0;
  col.quoted = quoted_status;
  return col;
}

// --- Combined Processor ---
void process_buffer(const uint8_t *data, size_t len, process_ctx_t *ctx) {
  size_t i = 0;

  // Handle CRLF split across buffer boundary - skip the '\n'
  if (len > 0 && ctx->last_char_was_cr && data[0] == '\n') {
      i = 1;
      ctx->row_start_offset = 1;
  }
  ctx->current_buffer = data;
  ctx->last_newline_idx = -1;
  ctx->total_col_offsets = 0;
  ctx->current_comma_idx = 0;

  // Cache local state
  uint64_t current_total_rows = ctx->total_rows;
  int current_inside_quote = ctx->inside_quote;
  int last_char_was_newline = ctx->last_char_was_newline;
  newline_callback_t cb = ctx->on_record;

  // 1. Define vectors for Line Feeds (LF), Carriage Returns (CR), Quotes and Commas
  const uint8x16_t vec_lf = vdupq_n_u8('\n');
  const uint8x16_t vec_cr = vdupq_n_u8('\r');
  const uint8x16_t vec_qt = vdupq_n_u8('"');
  const uint8x16_t vec_cm = vdupq_n_u8(',');

  for (; i + 64 <= len; i += 64) {
    uint8x16_t b0 = vld1q_u8(data + i);
    uint8x16_t b1 = vld1q_u8(data + i + 16);
    uint8x16_t b2 = vld1q_u8(data + i + 32);
    uint8x16_t b3 = vld1q_u8(data + i + 48);

    // 2. Calculate Quotes & Newlines
    uint64_t n0_lf= neon_movemask(vceqq_u8(b0, vec_lf));
    uint64_t n0_cr = neon_movemask(vceqq_u8(b0, vec_cr));

    uint64_t n1_lf = neon_movemask(vceqq_u8(b1, vec_lf));
    uint64_t n1_cr = neon_movemask(vceqq_u8(b1, vec_cr));

    uint64_t n2_lf = neon_movemask(vceqq_u8(b2, vec_lf));
    uint64_t n2_cr = neon_movemask(vceqq_u8(b2, vec_cr));

    uint64_t n3_lf = neon_movemask(vceqq_u8(b3, vec_lf));
    uint64_t n3_cr = neon_movemask(vceqq_u8(b3, vec_cr));

    uint64_t mask_lf = n0_lf | (n1_lf << 16) | (n2_lf << 32) | (n3_lf << 48);
    uint64_t mask_cr = n0_cr | (n1_cr << 16) | (n2_cr << 32) | (n3_cr << 48);

    uint64_t newlines = mask_lf | mask_cr;

    uint64_t q0 = neon_movemask(vceqq_u8(b0, vec_qt));
    uint64_t q1 = neon_movemask(vceqq_u8(b1, vec_qt));
    uint64_t q2 = neon_movemask(vceqq_u8(b2, vec_qt));
    uint64_t q3 = neon_movemask(vceqq_u8(b3, vec_qt));

    uint64_t quotes   = q0 | (q1 << 16) | (q2 << 32) | (q3 << 48);

    // 3. State Machine Logic
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

    // 4. Conditional Comma Processing
    if (cb) {
        uint64_t c0 = neon_movemask(vceqq_u8(b0, vec_cm));
        uint64_t c1 = neon_movemask(vceqq_u8(b1, vec_cm));
        uint64_t c2 = neon_movemask(vceqq_u8(b2, vec_cm));
        uint64_t c3 = neon_movemask(vceqq_u8(b3, vec_cm));
        uint64_t commas = c0 | (c1 << 16) | (c2 << 32) | (c3 << 48);

        uint64_t valid_commas = commas & ~state_mask;

        // We need a local copy of quotes to consume bits as we process columns
        uint64_t current_blk_quotes = quotes;

        if (valid_commas) {
            if (ctx->total_col_offsets + 64 >= ctx->col_capacity) {
                ctx->col_capacity *= 2;
                ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
            }

            while (valid_commas) {
                int bit_idx = __builtin_ctzll(valid_commas);
                uint64_t mask_upto = (1ULL << bit_idx) - 1;

                // Check if we have accumulated quotes from previous blocks OR quotes in this block before the comma
                bool is_quoted = (ctx->accumulated_quotes != 0) || ((current_blk_quotes & mask_upto) != 0);

                // Store offset with the quoted flag in the MSB
                ctx->col_offsets[ctx->total_col_offsets++] = (i + bit_idx) | (is_quoted ? COL_QUOTED_FLAG : 0);

                // Clear the quotes we just accounted for (up to and including this comma)
                current_blk_quotes &= ~mask_upto;
                current_blk_quotes &= ~(1ULL << bit_idx);

                // Reset accumulator for the next column
                ctx->accumulated_quotes = 0;
                valid_commas &= (valid_commas - 1);
            }
        }

        // 4. Newline Processing (Callback aware)
        uint64_t valid_newlines = newlines & ~state_mask;

        if (valid_newlines) {
          int last_bit = 63 - __builtin_clzll(valid_newlines);
          ctx->last_newline_idx = i + last_bit;

          uint64_t events = valid_newlines;
          while (events) {
            int bit_idx = __builtin_ctzll(events);
            size_t abs_offset = i + bit_idx;

            // Check if this is a \r followed immediately by \n
            if (data[abs_offset] == '\r') {
                if (abs_offset + 1 < len && data[abs_offset + 1] == '\n') {
                    // It is a \r\n sequence. Ignore this \r.
                    // The \n (which is the next bit in 'events') will handle the row.

                    // 1. Clean up quote tracking so it doesn't bleed into the next bit
                    uint64_t mask_upto = (1ULL << bit_idx) - 1;
                    current_blk_quotes &= ~mask_upto;
                    current_blk_quotes &= ~(1ULL << bit_idx);

                    // 2. Clear this bit and continue loop
                    events &= (events - 1);
                    continue;
                }
            }

            // Determine quote status for the last column (ending at this newline)
            uint64_t mask_upto = (1ULL << bit_idx) - 1;
            ctx->last_col_quoted = (ctx->accumulated_quotes != 0) || ((current_blk_quotes & mask_upto) != 0);

            current_total_rows++;
            ctx->total_rows = current_total_rows;
            ctx->row_end_offset = abs_offset;

            cb(ctx);

            ctx->row_start_offset = abs_offset + 1;

            // Clear quotes up to this newline for the next row
            current_blk_quotes &= ~mask_upto;
            current_blk_quotes &= ~(1ULL << bit_idx);
            ctx->accumulated_quotes = 0;

            // Sync comma cursor
            while (ctx->current_comma_idx < ctx->total_col_offsets &&
                   (ctx->col_offsets[ctx->current_comma_idx] & COL_OFFSET_MASK) < ctx->row_start_offset) {
              ctx->current_comma_idx++;
            }

            events &= (events - 1);
          }
        }

        // After processing all delimiters in this block, if any quotes remain,
        // they belong to the next column (which continues into the next block)
        if (current_blk_quotes) {
            ctx->accumulated_quotes = 1;
        }

    } else {
        // Fast path: No callback, just counting rows
        // 1. Filter out quotes
        uint64_t valid_newlines = newlines & ~state_mask;

        if (valid_newlines) {
            // 1. Identify "Internal" CRLF pairs (CR at bit N, LF at bit N+1)
            // Logic: mask_lf >> 1 shifts the LF bit 'back' to the position of the CR.
            // intersection finds CRs that have an LF immediately after them.
            uint64_t ignore_mask = mask_cr & (mask_lf >> 1);
            valid_newlines &= ~ignore_mask;

            // 2. Identify "Boundary" CRLF (CR at bit 63, LF at bit 0 of next block)
            // If the last bit is a valid CR...
            if ((valid_newlines & (1ULL << 63)) && (mask_cr & (1ULL << 63))) {
                 // Peek at the next byte in memory to see if it's an LF
                 if (i + 64 < len && data[i + 64] == '\n') {
                     // It is a pair. Ignore the CR here. The next block will count the LF.
                     valid_newlines &= ~(1ULL << 63);
                 }
            }

            // Now count safely
            if (valid_newlines) {
                int last_bit = 63 - __builtin_clzll(valid_newlines);
                ctx->last_newline_idx = i + last_bit;
                current_total_rows += __builtin_popcountll(valid_newlines);
            }
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
    bool is_comma = (data[i] == ',');

    bool is_newline = (data[i] == '\n' || data[i] == '\r');
    // If it is \r, check if \n follows (CRLF)
    if (data[i] == '\r') {
        if (i + 1 < len && data[i+1] == '\n') {
            // It is \r\n. Treat the \r as a non-newline char (whitespace),
            // so we don't trigger the row callback yet.
            is_newline = false;
        }
    }
    if (is_quote) {
      // Accumulate quote status for current column
      if (cb) ctx->accumulated_quotes = 1;

      if (ctx->inside_quote) {
        ctx->inside_quote = 0;
      } else {
        if (ctx->last_char_was_newline) {
          ctx->inside_quote = 1;
        }
      }
    } else if (!ctx->inside_quote) {
        // Handle Commas
        if (cb && is_comma) {
            if (ctx->total_col_offsets >= ctx->col_capacity) {
                ctx->col_capacity *= 2;
                ctx->col_offsets = realloc(ctx->col_offsets, ctx->col_capacity * sizeof(uint64_t));
            }
            // Store with flag
            ctx->col_offsets[ctx->total_col_offsets++] = i | (ctx->accumulated_quotes ? COL_QUOTED_FLAG : 0);
            ctx->accumulated_quotes = 0; // Reset
        }

        // Handle Newlines
        if (is_newline) {
            ctx->total_rows++;
            ctx->last_newline_idx = i;

            if (cb) {
                ctx->last_col_quoted = (ctx->accumulated_quotes != 0);
                ctx->row_end_offset = i;
                cb(ctx);
                ctx->row_start_offset = i + 1;
                ctx->accumulated_quotes = 0; // Reset for next row

                while (ctx->current_comma_idx < ctx->total_col_offsets &&
                    (ctx->col_offsets[ctx->current_comma_idx] & COL_OFFSET_MASK) < ctx->row_start_offset) {
                  ctx->current_comma_idx++;
                }
            }
        }
    }

    ctx->last_char_was_newline = is_newline;
  }
  ctx->last_char_was_cr = (len > 0 && data[len-1] == '\r');
}

static void print_some_columns(void *ctx_void) {
  process_ctx_t *ctx = (process_ctx_t*)ctx_void;

  // Example usage: print column content AND quoted status
  const size_t out_cols[] = { 0, 1, 2, 5, 4 };
  const size_t out_colcount = 5;
  for(size_t i = 0; i < out_colcount; i++) {
    column_t c = get_column(ctx, out_cols[i]);
    zsv_writer_cell(ctx->writer, i == 0, (const unsigned char *)c.str, c.len, c.quoted);
  }
}

int main(int argc, const char *argv[]) {
  static uint8_t buffer[BUF_SIZE] __attribute__((aligned(64)));
  newline_callback_t cb = NULL;

  if(argc > 1) {
    if(!strcmp(argv[1], "test1"))
      cb = print_some_columns;
    else {
      fprintf(stderr, "Usage: %s [test1]\n", argv[0]);
      return 1;
    }
  }

  process_ctx_t ctx = {
    .total_rows = 0,
    .inside_quote = 0,
    .last_char_was_newline = 1,
    .last_char_was_cr = 0,
    .last_newline_idx = -1,
    .accumulated_quotes = 0, // Init
    .last_col_quoted = false,

    .on_record = cb,

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
    char extra_newline_added = 0;
    size_t bytes_read = 0;
    if (!force_flush) {
      bytes_read = fread(buffer + valid_bytes, 1, space_available, stdin);
      if (bytes_read == 0 && valid_bytes == 0) break;
    }

    size_t total_in_buffer = valid_bytes + bytes_read + extra_newline_added;

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

        // TO DO: handle EOF with partial line: invoke row callback:
        //   if(bytes_read == 0 && valid_bytes > 0 && have_data)
        //     ctx.on_record(&ctx);

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

#endif
