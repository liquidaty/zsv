/*
 * Branchless fast CSV scanner.
 *
 * Uses 64-byte SIMD processing to find delimiters and row-ends in bulk,
 * with prefix-XOR carry propagation for branchless quote state tracking.
 *
 * In the common case (no quotes), cells are stored directly without
 * going through cell_dl(), eliminating per-cell function call and
 * branch overhead. Quoted cells still use cell_dl() for normalization.
 *
 * Platform-specific SIMD intrinsics are isolated in zsv_scan_simd_*.h
 * headers, each providing a uniform interface:
 *   fast_vec_t        — broadcast vector type
 *   fast_vec_set1(c)  — broadcast byte c to all lanes
 *   fast_cmpeq_64(p,v)— compare 64 bytes at p against v, return 64-bit mask
 *
 * Falls back to the standard engine on unsupported platforms.
 */

#if defined(__aarch64__)
#include "zsv_scan_simd_neon.h"
#define ZSV_FAST_PARSER_AVAILABLE 1
#elif defined(__AVX2__)
#include "zsv_scan_simd_avx2.h"
#define ZSV_FAST_PARSER_AVAILABLE 1
#elif defined(__x86_64__) || defined(_M_X64)
#include "zsv_scan_simd_sse2.h"
#define ZSV_FAST_PARSER_AVAILABLE 1
#endif

#ifdef ZSV_FAST_PARSER_AVAILABLE

/*
 * Set scanner->quoted and scanner->quote_close_position for a cell
 * by scanning its content. This replicates the quote tracking that
 * the standard engine does character-by-character.
 */
__attribute__((always_inline)) static inline void fast_set_quote_flags(struct zsv_scanner *scanner, unsigned char *s,
                                                                       size_t n) {
  scanner->quote_close_position = 0;
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
 * Store a raw cell placeholder into the row. No normalization, no
 * cell_handler callback. Used for columns that the caller doesn't need.
 */
__attribute__((always_inline)) static inline void fast_store_cell_raw(struct zsv_scanner *scanner, unsigned char *s,
                                                                      size_t n) {
  if (VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
    struct zsv_cell c = {s, n, scanner->opts.no_quotes ? 1 : 0, 0};
    scanner->row.cells[scanner->row.used++] = c;
  } else
    scanner->row.overflow++;
  scanner->have_cell = 1;
}

/*
 * Store a cell directly into the row, bypassing cell_dl().
 * Used in the fast path when we know there are no quotes to normalize.
 * The 'quoted' field is set to 1 when no_quotes mode is active (matching
 * cell_dl behavior), or 0 otherwise.
 */
__attribute__((always_inline)) static inline void fast_store_cell(struct zsv_scanner *scanner, unsigned char *s,
                                                                  size_t n) {
  /* If column filter is active, check if this column needs full processing */
  if (scanner->needed_cols) {
    if (scanner->row.used >= scanner->needed_cols_count || !scanner->needed_cols[scanner->row.used]) {
      fast_store_cell_raw(scanner, s, n);
      return;
    }
  }
  if (scanner->opts.malformed_utf8_replace) {
    if (scanner->opts.malformed_utf8_replace < 0)
      n = zsv_strencode(s, n, 0, NULL, NULL);
    else
      n = zsv_strencode(s, n, scanner->opts.malformed_utf8_replace, NULL, NULL);
  }
  if (UNLIKELY(scanner->opts.cell_handler != NULL))
    scanner->opts.cell_handler(scanner->opts.ctx, s, n);
  if (VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
    struct zsv_cell c = {s, n, scanner->opts.no_quotes ? 1 : 0, 0};
    scanner->row.cells[scanner->row.used++] = c;
  } else
    scanner->row.overflow++;
  scanner->have_cell = 1;
}

/*
 * Store a cell and invoke the row handler, bypassing cell_dl()/row_dl().
 * Returns non-zero status on abort/cancellation.
 */
__attribute__((always_inline)) static inline enum zsv_status fast_store_cell_and_row(struct zsv_scanner *scanner,
                                                                                     unsigned char *s, size_t n) {
  fast_store_cell(scanner, s, n);

  if (VERY_UNLIKELY(scanner->row.overflow)) {
    scanner->errprintf(scanner->errf, "Warning: number of columns (%zu) exceeds row max (%zu)\n",
                       scanner->row.allocated + scanner->row.overflow, scanner->row.allocated);
    scanner->row.overflow = 0;
  }
  if (VERY_LIKELY(scanner->opts.row_handler != NULL))
    scanner->opts.row_handler(scanner->opts.ctx);

#ifdef ZSV_EXTRAS
  scanner->progress.cum_row_count++;
  if (VERY_UNLIKELY(scanner->opts.progress.rows_interval &&
                    scanner->progress.cum_row_count % scanner->opts.progress.rows_interval == 0)) {
    char ok;
    if (!scanner->opts.progress.seconds_interval)
      ok = 1;
    else {
      time_t now = time(NULL);
      if (now > scanner->progress.last_time &&
          (unsigned int)(now - scanner->progress.last_time) >= scanner->opts.progress.seconds_interval) {
        ok = 1;
        scanner->progress.last_time = now;
      } else
        ok = 0;
    }
    if (ok && scanner->opts.progress.callback)
      scanner->abort = scanner->opts.progress.callback(scanner->opts.progress.ctx, scanner->progress.cum_row_count);
  }
  if (VERY_UNLIKELY(scanner->progress.max_rows > 0)) {
    if (VERY_UNLIKELY(scanner->progress.cum_row_count == scanner->progress.max_rows)) {
      scanner->abort = 1;
      scanner->row.used = 0;
      return zsv_status_max_rows_read;
    }
  }
#endif

  if (VERY_UNLIKELY(scanner->abort))
    return zsv_status_cancelled;
  scanner->have_cell = 0;
  scanner->row.used = 0;
  return zsv_status_ok;
}

/*
 * Handle a row-end for an unquoted cell (fast path).
 * Skips cell_dl()/row_dl() overhead.
 */
#define FAST_ROWEND_NOQUOTE(scanner, buff, idx, is_cr)                                                                 \
  do {                                                                                                                 \
    if (!(is_cr)) {                                                                                                    \
      char prev = (idx) > 0 ? (buff)[(idx)-1] : (scanner)->last;                                                       \
      if (prev == '\r') {                                                                                              \
        (scanner)->cell_start = (idx) + 1;                                                                             \
        (scanner)->row_start = (idx) + 1;                                                                              \
        break;                                                                                                         \
      }                                                                                                                \
    }                                                                                                                  \
    (scanner)->scanned_length = (idx);                                                                                 \
    enum zsv_status stat_ =                                                                                            \
      fast_store_cell_and_row((scanner), (buff) + (scanner)->cell_start, (idx) - (scanner)->cell_start);               \
    if (VERY_UNLIKELY(stat_))                                                                                          \
      return stat_;                                                                                                    \
    (scanner)->cell_start = (idx) + 1;                                                                                 \
    (scanner)->row_start = (idx) + 1;                                                                                  \
    (scanner)->data_row_count++;                                                                                       \
  } while (0)

/*
 * Handle a row-end for a potentially quoted cell.
 * Sets quote flags, then calls cell_and_row_dl().
 */
#define FAST_ROWEND_QUOTED(scanner, buff, idx, is_cr, quote_char)                                                      \
  do {                                                                                                                 \
    if (!(is_cr)) {                                                                                                    \
      char prev = (idx) > 0 ? (buff)[(idx)-1] : (scanner)->last;                                                       \
      if (prev == '\r') {                                                                                              \
        (scanner)->cell_start = (idx) + 1;                                                                             \
        (scanner)->row_start = (idx) + 1;                                                                              \
        break;                                                                                                         \
      }                                                                                                                \
    }                                                                                                                  \
    (scanner)->scanned_length = (idx);                                                                                 \
    if ((quote_char) > 0)                                                                                              \
      fast_set_quote_flags((scanner), (buff) + (scanner)->cell_start, (idx) - (scanner)->cell_start);                  \
    enum zsv_status stat_ = cell_and_row_dl((scanner), (buff) + (scanner)->cell_start, (idx) - (scanner)->cell_start); \
    if (VERY_UNLIKELY(stat_))                                                                                          \
      return stat_;                                                                                                    \
    (scanner)->cell_start = (idx) + 1;                                                                                 \
    (scanner)->row_start = (idx) + 1;                                                                                  \
    (scanner)->data_row_count++;                                                                                       \
  } while (0)

static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  /* Guard: fall back for unsupported configurations */
  if (0
#ifndef ZSV_NO_ONLY_CRLF
      || scanner->opts.only_crlf_rowend /* only-crlf mode — not yet supported */
#endif
  ) {
    return zsv_scan_delim(scanner, buff, bytes_read);
  }

  int quote_char = scanner->opts.no_quotes > 0 ? -1 : '"';

  size_t i = scanner->partial_row_length;

  /* If the entire buffer fits in the scalar tail and contains quotes,
   * use the legacy engine. The scalar tail's simplified quote handling
   * combined with cell_dl's in-place memmove can produce incorrect results
   * for small inputs with complex quoting. No performance impact since
   * this only triggers for inputs < 64 bytes after partial row data. */
  /* Note: non-standard quoting (mid-cell quotes in unquoted cells) is
   * detected per-block in the SIMD loop and falls back to the scalar
   * tail which uses the cell_start check for correct handling. */

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
  char delimiter = scanner->opts.delimiter;

  /* Carry bits for non-standard quoting detection across 64-byte blocks */
  int last_was_delim;
  int last_was_quote = 0;
  if (i == 0)
    last_was_delim = 1;
  else {
    unsigned char prev = buff[i - 1];
    last_was_delim = (prev == delimiter || prev == '\n' || prev == '\r');
    last_was_quote = (prev == '"' && quote_char > 0);
  }
  fast_vec_t v_comma = fast_vec_set1((unsigned char)delimiter);
  fast_vec_t v_nl = fast_vec_set1('\n');
  fast_vec_t v_cr = fast_vec_set1('\r');
  fast_vec_t v_qt = fast_vec_set1(quote_char > 0 ? (unsigned char)quote_char : 0);

  /*
   * Skip-cells mode: no cell storage, just count rows.
   * Used when the caller doesn't need cell data (e.g. count, skip-head).
   *
   * Uses SIMD to find row-ends and quotes in bulk. For blocks without
   * quotes (common case), counts row-ends via popcount. For blocks
   * with quotes, falls through to scalar processing.
   */
  if (scanner->skip_cells) {
    while (i + 64 <= bytes_read) {
      uint64_t newlines = fast_cmpeq_64(buff + i, v_nl);
      uint64_t crs = fast_cmpeq_64(buff + i, v_cr);
      uint64_t quotes = quote_char > 0 ? fast_cmpeq_64(buff + i, v_qt) : 0;

      if (LIKELY(quotes == 0 && !inside_quote)) {
        /* No quote state to track. Count row-ends via popcount. */
        uint64_t crlf_n = (crs << 1) & newlines;
        uint64_t row_ends = crs | (newlines & ~crlf_n);

        if (row_ends) {
          int nrows = __builtin_popcountll(row_ends);
          int last_bit = 63 - __builtin_clzll(row_ends);
          size_t last_rowend_pos = i + last_bit;

          for (int r = 0; r < nrows; r++) {
            scanner->data_row_count++;
            if (VERY_LIKELY(scanner->opts.row_handler != NULL))
              scanner->opts.row_handler(scanner->opts.ctx);
            scanner->have_cell = 0;
            scanner->row.used = 0;
#ifdef ZSV_EXTRAS
            scanner->progress.cum_row_count++;
            if (VERY_UNLIKELY(scanner->progress.max_rows > 0 &&
                              scanner->progress.cum_row_count == scanner->progress.max_rows)) {
              scanner->abort = 1;
              return zsv_status_max_rows_read;
            }
#endif
            if (VERY_UNLIKELY(scanner->abort))
              return zsv_status_cancelled;
          }
          scanner->cell_start = last_rowend_pos + 1;
          scanner->row_start = last_rowend_pos + 1;
        }
        i += 64;
        if (!scanner->skip_cells)
          break;
        continue;
      }

      /* Block has quotes or we're inside a quoted cell.
       * Check for non-standard quoting first. Need comma/tab bitmask. */
      uint64_t sc_commas = fast_cmpeq_64(buff + i, v_comma);
      uint64_t all_sc_delims = sc_commas | newlines | crs;
      unsigned char prev_byte = i > 0 ? buff[i - 1] : '\n';
      int prev_is_delim = (prev_byte == delimiter || prev_byte == '\n' || prev_byte == '\r');
      uint64_t sc_cell_starts = (all_sc_delims << 1) | (prev_is_delim ? 1ULL : 0ULL);
      uint64_t sc_adj = ((quotes << 1) | ((prev_byte == '"') ? 1ULL : 0ULL)) | ((quotes >> 1) | (1ULL << 63));
      uint64_t sc_before_delim = (all_sc_delims >> 1) | (1ULL << 63);
      uint64_t sc_nonstandard = quotes & ~(sc_cell_starts | sc_adj | sc_before_delim);

      if (UNLIKELY(sc_nonstandard != 0))
        /* Non-standard quoting — fall through to scalar tail */
        break;

      /* Use prefix-XOR to mask out newlines inside quotes.
       * PCLMULQDQ on x86 reduces this to a single instruction. */
      uint64_t B = fast_prefix_xor(quotes);
      uint64_t state_mask = inside_quote ? ~B : B;
      inside_quote = (state_mask >> 63) & 1;

      uint64_t valid_nl = newlines & ~state_mask;
      uint64_t valid_cr = crs & ~state_mask;
      uint64_t crlf_n = (valid_cr << 1) & valid_nl;
      uint64_t row_ends = valid_cr | (valid_nl & ~crlf_n);

      if (row_ends) {
        int nrows = __builtin_popcountll(row_ends);
        int last_bit = 63 - __builtin_clzll(row_ends);
        size_t last_rowend_pos = i + last_bit;

        for (int r = 0; r < nrows; r++) {
          scanner->data_row_count++;
          if (VERY_LIKELY(scanner->opts.row_handler != NULL))
            scanner->opts.row_handler(scanner->opts.ctx);
          scanner->have_cell = 0;
          scanner->row.used = 0;
#ifdef ZSV_EXTRAS
          scanner->progress.cum_row_count++;
          if (VERY_UNLIKELY(scanner->progress.max_rows > 0 &&
                            scanner->progress.cum_row_count == scanner->progress.max_rows)) {
            scanner->abort = 1;
            return zsv_status_max_rows_read;
          }
#endif
          if (VERY_UNLIKELY(scanner->abort))
            return zsv_status_cancelled;
        }
        scanner->cell_start = last_rowend_pos + 1;
        scanner->row_start = last_rowend_pos + 1;
      }
      i += 64;
      if (!scanner->skip_cells)
        break;
      continue;
    }

    /* Scalar tail for skip_cells: handles remaining bytes */
    if (scanner->skip_cells) {
      for (; i < bytes_read; i++) {
        unsigned char c = buff[i];
        if (c == '"' && quote_char > 0) {
          if (inside_quote) {
            if (i + 1 < bytes_read && buff[i + 1] == '"')
              i++;
            else
              inside_quote = 0;
          } else
            inside_quote = 1;
          continue;
        }
        if (inside_quote)
          continue;
        if (c == '\r' || (c == '\n' && (i == 0 ? scanner->last != '\r' : buff[i - 1] != '\r'))) {
          scanner->data_row_count++;
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
          if (VERY_LIKELY(scanner->opts.row_handler != NULL))
            scanner->opts.row_handler(scanner->opts.ctx);
          scanner->have_cell = 0;
          scanner->row.used = 0;
#ifdef ZSV_EXTRAS
          scanner->progress.cum_row_count++;
          if (VERY_UNLIKELY(scanner->progress.max_rows > 0 &&
                            scanner->progress.cum_row_count == scanner->progress.max_rows)) {
            scanner->abort = 1;
            return zsv_status_max_rows_read;
          }
#endif
          if (VERY_UNLIKELY(scanner->abort))
            return zsv_status_cancelled;
          if (!scanner->skip_cells)
            break;
        } else if (c == '\n') {
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
        }
      }
    }

    if (!scanner->skip_cells && i < bytes_read) {
      inside_quote = (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) ? 1 : 0;
      goto normal_parse;
    }

    if (inside_quote)
      scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
    else
      scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;
    scanner->scanned_length = i;
    scanner->old_bytes_read = bytes_read;
    return zsv_status_ok;
  }

normal_parse:
  /* Process 64 bytes at a time */
  while (i + 64 <= bytes_read) {
    uint64_t commas, newlines, crs, quotes;
    fast_scan_block(buff + i, v_comma, v_nl, v_cr, v_qt, &commas, &newlines, &crs, &quotes);
    if (quote_char <= 0)
      quotes = 0;

    uint64_t all_delims = commas | newlines | crs;

    if (LIKELY(quotes == 0 && !inside_quote)) {
      /*
       * Fast path: no quotes in this 64-byte chunk and not inside a quoted
       * cell. Store cells directly without going through cell_dl().
       */
      last_was_delim = (all_delims >> 63) & 1;
      last_was_quote = 0;
      if (LIKELY(all_delims == 0)) {
        last_was_delim = 0;
        i += 64;
        continue;
      }

      size_t base = i;
      i += 64;

      while (all_delims) {
        int bit = __builtin_ctzll(all_delims);
        size_t idx = base + bit;
        uint64_t bitmask = 1ULL << bit;
        all_delims = fast_clear_lowest(all_delims);

        if (LIKELY(bitmask & commas)) {
          scanner->scanned_length = idx;
          fast_store_cell(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
          scanner->cell_start = idx + 1;
        } else if (bitmask & crs) {
          FAST_ROWEND_NOQUOTE(scanner, buff, idx, 1);
        } else {
          FAST_ROWEND_NOQUOTE(scanner, buff, idx, 0);
        }
      }
      continue;
    }

    /*
     * Quote-aware path.
     */
    /*
     * Check for non-standard quoting: a quote is "standard" if it's
     * an opener (preceded by delimiter), part of an escape pair
     * (adjacent to another quote), or a closer (followed by delimiter).
     * Any other quote is non-standard — break out of the SIMD loop
     * and let the scalar tail handle the rest of this buffer.
     */
    uint64_t cell_starts = (all_delims << 1) | (last_was_delim ? 1ULL : 0ULL);
    uint64_t adj_to_quote = ((quotes << 1) | (last_was_quote ? 1ULL : 0ULL)) |
                            ((quotes >> 1) | (1ULL << 63)); /* bit 63: can't see next block */
    uint64_t before_delim = (all_delims >> 1) | (1ULL << 63);
    uint64_t standard_pos = cell_starts | adj_to_quote | before_delim;
    uint64_t nonstandard = quotes & ~standard_pos;

    last_was_delim = (all_delims >> 63) & 1;
    last_was_quote = (quotes >> 63) & 1;

    if (UNLIKELY(nonstandard != 0)) {
      /* Non-standard quoting detected in this 64-byte block.
       * Process it character-by-character using the same logic as the
       * scalar tail (cell_start check for quote openers), then continue
       * with the SIMD loop for subsequent blocks. */
      size_t block_end = i + 64;
      for (; i < block_end; i++) {
        unsigned char c = buff[i];
        if (c == '"' && quote_char > 0) {
          if (inside_quote) {
            if (i + 1 < bytes_read && buff[i + 1] == '"') {
              i++;
            } else {
              inside_quote = 0;
            }
          } else if (i == scanner->cell_start) {
            inside_quote = 1;
          }
          continue;
        }
        if (inside_quote)
          continue;
        if (c == delimiter) {
          scanner->scanned_length = i;
          /* Use fast_set_quote_flags to set flags, then store via cell_dl.
           * cell_dl's memmove only modifies bytes within [cell_start, i)
           * which we've already passed — safe for forward iteration. */
          if (quote_char > 0)
            fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
          cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
          scanner->cell_start = i + 1;
        } else if (c == '\r') {
          FAST_ROWEND_QUOTED(scanner, buff, i, 1, quote_char);
        } else if (c == '\n') {
          FAST_ROWEND_QUOTED(scanner, buff, i, 0, quote_char);
        }
      }
      continue;
    }

    /*
     * Standard quoting: use prefix-XOR to compute state_mask.
     * Every quote toggles the inside/outside state.
     * PCLMULQDQ on x86 reduces this to a single instruction.
     */
    uint64_t B = fast_prefix_xor(quotes);
    uint64_t state_mask = inside_quote ? ~B : B;
    inside_quote = (state_mask >> 63) & 1;

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
      valid_delims = fast_clear_lowest(valid_delims);

      if (LIKELY(bitmask & commas)) {
        scanner->scanned_length = idx;
        if (scanner->needed_cols &&
            (scanner->row.used >= scanner->needed_cols_count || !scanner->needed_cols[scanner->row.used])) {
          fast_store_cell_raw(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
        } else {
          fast_set_quote_flags(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
          cell_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
        }
        scanner->cell_start = idx + 1;
      } else if (bitmask & crs) {
        FAST_ROWEND_QUOTED(scanner, buff, idx, 1, quote_char);
      } else {
        FAST_ROWEND_QUOTED(scanner, buff, idx, 0, quote_char);
      }
    }
  }

  /* Scalar tail — process remaining bytes one at a time. */
  for (; i < bytes_read; i++) {
    unsigned char c = buff[i];

    if (c == '"' && quote_char > 0) {
      if (inside_quote) {
        if (i + 1 < bytes_read && buff[i + 1] == '"') {
          i++; /* skip escaped quote */
        } else {
          inside_quote = 0;
        }
      } else if (i == scanner->cell_start) {
        inside_quote = 1;
      }
      /* else: mid-cell quote in unquoted cell — ignore for delimiter detection.
       * fast_set_quote_flags() will handle it correctly for normalization. */
      continue;
    }

    if (inside_quote)
      continue;

    if (c == delimiter) {
      scanner->scanned_length = i;
      /* Use fast_set_quote_flags + cell_dl for cell normalization.
       * cell_dl's memmove is safe here because it only shifts bytes
       * within [cell_start, i), which precedes the current position. */
      if (quote_char > 0)
        fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
      cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
      scanner->cell_start = i + 1;
    } else if (c == '\r') {
      FAST_ROWEND_QUOTED(scanner, buff, i, 1, quote_char);
    } else if (c == '\n') {
      FAST_ROWEND_QUOTED(scanner, buff, i, 0, quote_char);
    }
  }

  /* Carry quote state across buffer boundaries */
  if (inside_quote)
    scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
  else
    scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;

  /* If there's remaining cell data (no trailing row-end), set quote
   * flags so zsv_finish can properly normalize the final cell. */
  if (quote_char > 0 && i > scanner->cell_start && !inside_quote)
    fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);

  scanner->scanned_length = i;
  scanner->old_bytes_read = bytes_read;
  return zsv_status_ok;
}

#undef FAST_ROWEND_NOQUOTE
#undef FAST_ROWEND_QUOTED

#else /* !ZSV_FAST_PARSER_AVAILABLE */
/* Unsupported platform: fall back to standard engine */
static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  return zsv_scan_delim(scanner, buff, bytes_read);
}
#endif
