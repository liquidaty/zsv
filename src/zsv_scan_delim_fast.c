/*
 * Branchless fast CSV scanner.
 *
 * Uses 64-byte SIMD processing to find delimiters and row-ends in bulk,
 * with prefix-XOR carry propagation for branchless quote state tracking.
 * Calls cell_dl() / cell_and_row_dl() for cell storage and normalization.
 *
 * Falls back to the standard engine for unsupported configurations.
 */

#if defined(__aarch64__)
#include <arm_neon.h>

static inline uint16_t fast_neon_movemask(uint8x16_t input) {
  static const uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                      1, 2, 4, 8, 16, 32, 64, 128};
  const uint8x16_t bit_weights = vld1q_u8(weights);
  uint8x16_t masked = vandq_u8(input, bit_weights);
  return (uint16_t)vaddv_u8(vget_low_u8(masked)) |
         ((uint16_t)vaddv_u8(vget_high_u8(masked)) << 8);
}

/*
 * Set scanner->quoted and scanner->quote_close_position for a cell
 * by scanning its content. This replicates the quote tracking that
 * the standard engine does character-by-character.
 */
__attribute__((always_inline))
static inline void fast_set_quote_flags(struct zsv_scanner *scanner, unsigned char *s, size_t n) {
  if (n == 0 || *s != '"') {
    /* Not a quoted cell. Check for embedded quotes (quote in unquoted cell) */
    if (n > 0 && memchr(s, '"', n))
      scanner->quoted = ZSV_PARSER_QUOTE_EMBEDDED;
    return;
  }

  /* Cell starts with a quote — find closing quote */
  scanner->quoted = ZSV_PARSER_QUOTE_UNCLOSED;
  scanner->quote_close_position = 0;

  for (size_t i = 1; i < n; i++) {
    if (s[i] == '"') {
      if (i + 1 < n && s[i + 1] == '"') {
        /* Embedded "" pair */
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
        scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
        i++; /* skip second quote */
      } else {
        /* Closing quote */
        scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
        scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;
        scanner->quote_close_position = i;
        break;
      }
    }
  }
}

/*
 * Handle a row-end character (\r or \n).
 * For \n after \r (CRLF), just advance past it without emitting a row.
 * Otherwise emit the row via cell_and_row_dl().
 */
#define FAST_HANDLE_ROWEND(scanner, buff, idx, is_cr)                          \
  do {                                                                         \
    if (!(is_cr)) {                                                            \
      /* \n: check if preceded by \r (CRLF pair — skip) */                     \
      char prev = (idx) > 0 ? (buff)[(idx) - 1] : (scanner)->last;            \
      if (prev == '\r') {                                                      \
        (scanner)->cell_start = (idx) + 1;                                     \
        (scanner)->row_start = (idx) + 1;                                      \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    (scanner)->scanned_length = (idx);                                         \
    enum zsv_status stat_ = cell_and_row_dl(                                   \
      (scanner), (buff) + (scanner)->cell_start, (idx) - (scanner)->cell_start \
    );                                                                         \
    if (VERY_UNLIKELY(stat_))                                                  \
      return stat_;                                                            \
    (scanner)->cell_start = (idx) + 1;                                         \
    (scanner)->row_start = (idx) + 1;                                          \
    (scanner)->data_row_count++;                                               \
  } while (0)

/*
 * Emit a cell: set quote flags from cell content, then call cell_dl().
 */
#define FAST_EMIT_CELL(scanner, buff, idx, quote_char)                         \
  do {                                                                         \
    (scanner)->scanned_length = (idx);                                         \
    if ((quote_char) > 0)                                                      \
      fast_set_quote_flags(                                                    \
        (scanner), (buff) + (scanner)->cell_start,                             \
        (idx) - (scanner)->cell_start);                                        \
    cell_dl((scanner), (buff) + (scanner)->cell_start,                         \
            (idx) - (scanner)->cell_start);                                    \
    (scanner)->cell_start = (idx) + 1;                                         \
  } while (0)

/*
 * Emit a row-end cell: set quote flags, then call cell_and_row_dl().
 */
#define FAST_EMIT_ROWEND(scanner, buff, idx, is_cr, quote_char)                \
  do {                                                                         \
    if (!(is_cr)) {                                                            \
      char prev = (idx) > 0 ? (buff)[(idx) - 1] : (scanner)->last;            \
      if (prev == '\r') {                                                      \
        (scanner)->cell_start = (idx) + 1;                                     \
        (scanner)->row_start = (idx) + 1;                                      \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    (scanner)->scanned_length = (idx);                                         \
    if ((quote_char) > 0)                                                      \
      fast_set_quote_flags(                                                    \
        (scanner), (buff) + (scanner)->cell_start,                             \
        (idx) - (scanner)->cell_start);                                        \
    enum zsv_status stat_ = cell_and_row_dl(                                   \
      (scanner), (buff) + (scanner)->cell_start, (idx) - (scanner)->cell_start \
    );                                                                         \
    if (VERY_UNLIKELY(stat_))                                                  \
      return stat_;                                                            \
    (scanner)->cell_start = (idx) + 1;                                         \
    (scanner)->row_start = (idx) + 1;                                          \
    (scanner)->data_row_count++;                                               \
  } while (0)

static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  /* Guard: fall back for unsupported configurations */
  if (scanner->opts.delimiter != ','   /* non-comma delimiter — not yet supported */
#ifndef ZSV_NO_ONLY_CRLF
      || scanner->opts.only_crlf_rowend   /* only-crlf mode — not yet supported */
#endif
      || scanner->opts.malformed_utf8_replace /* UTF-8 replacement — not yet supported */
      || scanner->opts.cell_handler       /* per-cell callback — not yet supported */
      ) {
    return zsv_scan_delim(scanner, buff, bytes_read);
  }

  int quote_char = scanner->opts.no_quotes > 0 ? -1 : '"';

  size_t i = scanner->partial_row_length;
  bytes_read += i;
  scanner->partial_row_length = 0;
  scanner->buffer_end = bytes_read;

  /* Handle ZSV_PARSER_QUOTE_PENDING from previous buffer */
  if (scanner->quoted & ZSV_PARSER_QUOTE_PENDING) {
    scanner->quoted -= ZSV_PARSER_QUOTE_PENDING;
    if (buff[i] != '"') {
      scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
      scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;
      scanner->quote_close_position = i - scanner->cell_start - 1;
    } else {
      scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
      scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
      i++;
    }
  }

  int inside_quote = (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) ? 1 : 0;

  uint8x16_t v_comma = vdupq_n_u8(',');
  uint8x16_t v_nl = vdupq_n_u8('\n');
  uint8x16_t v_cr = vdupq_n_u8('\r');
  uint8x16_t v_qt = vdupq_n_u8(quote_char > 0 ? (uint8_t)quote_char : 0);

  /* Process 64 bytes at a time */
  while (i + 64 <= bytes_read) {
    uint64_t commas = 0, newlines = 0, crs = 0, quotes = 0;
    for (int chunk = 0; chunk < 4; chunk++) {
      uint8x16_t b = vld1q_u8(buff + i + chunk * 16);
      unsigned shift = chunk * 16;
      commas   |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_comma)) << shift;
      newlines |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_nl)) << shift;
      crs      |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_cr)) << shift;
      if (quote_char > 0)
        quotes |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_qt)) << shift;
    }

    uint64_t all_delims = commas | newlines | crs;

    if (LIKELY(quotes == 0 && !inside_quote)) {
      /* Fast path: no quotes in chunk and not inside quoted cell */
      if (LIKELY(all_delims == 0)) {
        i += 64;
        continue;
      }

      size_t base = i;
      i += 64;

      while (all_delims) {
        int bit = __builtin_ctzll(all_delims);
        size_t idx = base + bit;
        uint64_t bitmask = 1ULL << bit;
        all_delims &= (all_delims - 1);

        if (LIKELY(bitmask & commas)) {
          FAST_EMIT_CELL(scanner, buff, idx, quote_char);
        } else if (bitmask & crs) {
          FAST_EMIT_ROWEND(scanner, buff, idx, 1, quote_char);
        } else {
          FAST_EMIT_ROWEND(scanner, buff, idx, 0, quote_char);
        }
      }
      continue;
    }

    /*
     * Quote-aware path: use prefix-XOR to compute state_mask.
     *
     * The state_mask has bit N set if position N is "inside quotes".
     * We use the carry-less prefix-XOR propagation trick:
     * each quote toggles the inside/outside state for all subsequent positions.
     */
    uint64_t A = ~0ULL;  /* "carry" propagation mask */
    uint64_t B = quotes; /* bits that toggle state */

    B = B ^ (A & (B << 1));  A = A & (A << 1);
    B = B ^ (A & (B << 2));  A = A & (A << 2);
    B = B ^ (A & (B << 4));  A = A & (A << 4);
    B = B ^ (A & (B << 8));  A = A & (A << 8);
    B = B ^ (A & (B << 16)); A = A & (A << 16);
    B = B ^ (A & (B << 32));

    /* If we entered this block inside a quote, invert the mask */
    uint64_t state_mask = inside_quote ? ~B : B;

    /* Update inside_quote for next block */
    inside_quote = (state_mask >> 63) & 1;

    /* Mask out delimiters that fall inside quotes */
    uint64_t valid_delims = all_delims & ~state_mask;

    if (LIKELY(valid_delims == 0)) {
      i += 64;
      continue;
    }

    size_t base = i;
    i += 64;

    while (valid_delims) {
      int bit = __builtin_ctzll(valid_delims);
      size_t idx = base + bit;
      uint64_t bitmask = 1ULL << bit;
      valid_delims &= (valid_delims - 1);

      if (LIKELY(bitmask & commas)) {
        FAST_EMIT_CELL(scanner, buff, idx, quote_char);
      } else if (bitmask & crs) {
        FAST_EMIT_ROWEND(scanner, buff, idx, 1, quote_char);
      } else {
        FAST_EMIT_ROWEND(scanner, buff, idx, 0, quote_char);
      }
    }
  }

  /* Scalar tail — process remaining bytes one at a time */
  for (; i < bytes_read; i++) {
    unsigned char c = buff[i];

    if (c == '"' && quote_char > 0) {
      if (inside_quote) {
        if (i + 1 < bytes_read && buff[i + 1] == '"') {
          i++; /* skip escaped quote */
        } else {
          inside_quote = 0;
        }
      } else {
        inside_quote = 1;
      }
      continue;
    }

    if (inside_quote)
      continue;

    if (c == ',') {
      FAST_EMIT_CELL(scanner, buff, i, quote_char);
    } else if (c == '\r') {
      FAST_EMIT_ROWEND(scanner, buff, i, 1, quote_char);
    } else if (c == '\n') {
      FAST_EMIT_ROWEND(scanner, buff, i, 0, quote_char);
    }
  }

  /* Handle quote pending at buffer boundary */
  if (inside_quote)
    scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;

  scanner->scanned_length = i;
  scanner->old_bytes_read = bytes_read;
  return zsv_status_ok;
}

#else
/* Non-aarch64: fall back to standard engine */
static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  return zsv_scan_delim(scanner, buff, bytes_read);
}
#endif
