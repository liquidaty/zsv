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
 * Process a 64-byte block character-by-character for malformed quoting mode.
 * Only quotes at cell_start open quoted fields; mid-cell quotes are ignored.
 * Used by both skip-cells and normal-parse paths.
 *
 * skip_cells_mode: if 1, only count rows (no cell storage).
 *                  if 0, store cells via fast_set_quote_flags + cell_dl.
 *
 * Returns: updated position i (past the processed block).
 * Sets *inside_quote_p to the quote state at the end of the block.
 * Returns (size_t)-1 on abort/max_rows to signal the caller to return.
 */
__attribute__((noinline, cold)) static size_t
fast_process_block_malformed(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read, size_t i,
                             int *inside_quote_p, int quote_char, char delimiter, int skip_cells_mode) {
  int inside_quote = *inside_quote_p;
  size_t block_end = i + 64;
  for (; i < block_end; i++) {
    unsigned char c = buff[i];
    if (c == '"' && quote_char > 0) {
      if (inside_quote) {
        if (i + 1 < bytes_read && buff[i + 1] == '"')
          i++;
        else
          inside_quote = 0;
      } else if (i == scanner->cell_start) {
        inside_quote = 1;
      }
      continue;
    }
    if (inside_quote)
      continue;
    if (c == delimiter) {
      if (skip_cells_mode) {
        scanner->cell_start = i + 1;
      } else {
        scanner->scanned_length = i;
        if (quote_char > 0)
          fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        scanner->cell_start = i + 1;
      }
    } else if (c == '\r') {
      if (skip_cells_mode) {
        scanner->data_row_count++;
        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
        if (VERY_LIKELY(scanner->opts.row_handler != NULL))
          scanner->opts.row_handler(scanner->opts.ctx);
        scanner->have_cell = 0;
        scanner->row.used = 0;
      } else {
        scanner->scanned_length = i;
        if (quote_char > 0)
          fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        enum zsv_status stat_ =
            cell_and_row_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        if (VERY_UNLIKELY(stat_)) {
          *inside_quote_p = inside_quote;
          return (size_t)-1;
        }
        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
        scanner->data_row_count++;
      }
#ifdef ZSV_EXTRAS
      scanner->progress.cum_row_count++;
      if (VERY_UNLIKELY(scanner->progress.max_rows > 0 &&
                        scanner->progress.cum_row_count == scanner->progress.max_rows)) {
        scanner->abort = 1;
        *inside_quote_p = inside_quote;
        return (size_t)-1;
      }
#endif
      if (VERY_UNLIKELY(scanner->abort)) {
        *inside_quote_p = inside_quote;
        return (size_t)-1;
      }
      if (skip_cells_mode && !scanner->skip_cells)
        break;
    } else if (c == '\n') {
      char prev = (i > 0) ? buff[i - 1] : scanner->last;
      if (prev == '\r') {
        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
      } else {
        if (skip_cells_mode) {
          scanner->data_row_count++;
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
          if (VERY_LIKELY(scanner->opts.row_handler != NULL))
            scanner->opts.row_handler(scanner->opts.ctx);
          scanner->have_cell = 0;
          scanner->row.used = 0;
        } else {
          scanner->scanned_length = i;
          if (quote_char > 0)
            fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
          enum zsv_status stat_ =
              cell_and_row_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
          if (VERY_UNLIKELY(stat_)) {
            *inside_quote_p = inside_quote;
            return (size_t)-1;
          }
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
          scanner->data_row_count++;
        }
#ifdef ZSV_EXTRAS
        scanner->progress.cum_row_count++;
        if (VERY_UNLIKELY(scanner->progress.max_rows > 0 &&
                          scanner->progress.cum_row_count == scanner->progress.max_rows)) {
          scanner->abort = 1;
          *inside_quote_p = inside_quote;
          return (size_t)-1;
        }
#endif
        if (VERY_UNLIKELY(scanner->abort)) {
          *inside_quote_p = inside_quote;
          return (size_t)-1;
        }
        if (skip_cells_mode && !scanner->skip_cells)
          break;
      }
    }
  }
  *inside_quote_p = inside_quote;
  return i;
}

/*
 * Out-of-line slow path for cell storage: handles column filtering,
 * UTF-8 replacement, and cell_handler callbacks. Kept out of the hot
 * SIMD loop to reduce code size and register pressure on x86-64.
 */
__attribute__((noinline)) static void fast_store_cell_slow(struct zsv_scanner *scanner, unsigned char *s, size_t n) {
  /* Column filter: skip unneeded columns */
  if (scanner->needed_cols) {
    if (scanner->row.used >= scanner->needed_cols_count || !scanner->needed_cols[scanner->row.used]) {
      if (VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
        struct zsv_cell c = {s, n, scanner->opts.no_quotes ? 1 : 0, 0};
        scanner->row.cells[scanner->row.used++] = c;
      } else
        scanner->row.overflow++;
      scanner->have_cell = 1;
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
 * Store a cell directly into the row, bypassing cell_dl().
 * Tiny inline fast path: just store pointer+length when no special
 * processing is needed. Falls back to out-of-line slow path for
 * column filtering, UTF-8 replacement, and cell_handler callbacks.
 */
__attribute__((always_inline)) static inline void fast_store_cell(struct zsv_scanner *scanner, unsigned char *s,
                                                                  size_t n) {
  if (UNLIKELY(scanner->needed_cols || scanner->opts.malformed_utf8_replace ||
               scanner->opts.cell_handler)) {
    fast_store_cell_slow(scanner, s, n);
    return;
  }
  if (VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
    struct zsv_cell c = {s, n, scanner->opts.no_quotes ? 1 : 0, 0};
    scanner->row.cells[scanner->row.used++] = c;
  } else
    scanner->row.overflow++;
  scanner->have_cell = 1;
}

/*
 * Out-of-line row-end handler: overflow warning, progress tracking,
 * max_rows check. Kept out of the hot loop.
 */
__attribute__((noinline)) static enum zsv_status fast_row_end_slow(struct zsv_scanner *scanner) {
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
 * Store a cell and invoke the row handler, bypassing cell_dl()/row_dl().
 * Returns non-zero status on abort/cancellation.
 */
__attribute__((always_inline)) static inline enum zsv_status fast_store_cell_and_row(struct zsv_scanner *scanner,
                                                                                     unsigned char *s, size_t n) {
  fast_store_cell(scanner, s, n);
  return fast_row_end_slow(scanner);
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
  int malformed_quoting = scanner->opts.malformed_quoting;

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
      uint64_t commas_sc, newlines, crs, quotes;
      fast_scan_block(buff + i, v_comma, v_nl, v_cr, v_qt, &commas_sc, &newlines, &crs, &quotes);
      if (quote_char <= 0)
        quotes = 0;

      /* Malformed quoting: fall back to scalar for blocks with quotes. */
      if (UNLIKELY(malformed_quoting) && (quotes || inside_quote)) {
        i = fast_process_block_malformed(scanner, buff, bytes_read, i, &inside_quote, quote_char, delimiter, 1);
        if (VERY_UNLIKELY(i == (size_t)-1))
          return scanner->abort ? zsv_status_cancelled : zsv_status_max_rows_read;
        if (!scanner->skip_cells)
          break;
        continue;
      }

      /* Unified path: prefix-XOR handles both quoted and unquoted blocks.
       * When quotes==0 and !inside_quote: B=0, state_mask=0, all delims valid.
       * When quotes or inside_quote: state_mask masks out delims inside quotes.
       * This eliminates the branch between quoted/unquoted paths, reducing
       * code size and improving branch prediction. */
      {
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

        if (UNLIKELY(malformed_quoting)) {
          uint64_t all_sc = commas_sc | newlines | crs;
          if (all_sc) {
            int last_delim = 63 - __builtin_clzll(all_sc);
            scanner->cell_start = i + last_delim + 1;
          }
        }

        i += 64;
        if (!scanner->skip_cells)
          break;
        continue;
      }
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
          } else if (malformed_quoting ? (i == scanner->cell_start) : 1) {
            inside_quote = 1;
          }
          continue;
        }
        if (inside_quote)
          continue;
        if (malformed_quoting && c == delimiter) {
          scanner->cell_start = i + 1;
        } else if (c == '\r' || (c == '\n' && (i == 0 ? scanner->last != '\r' : buff[i - 1] != '\r'))) {
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
      if (LIKELY(all_delims == 0)) {
        i += 64;
        continue;
      }

      size_t base = i;
      i += 64;

      /* When malformed_quoting is active, the first cell in this no-quote
       * block may span from a previous block that contained a mid-cell
       * quote. Check once whether cell_start precedes this block; if so,
       * use the full quote-aware path for that first cell only. */
      int cross_block_cell = malformed_quoting && (scanner->cell_start < base);

      while (all_delims) {
        int bit = __builtin_ctzll(all_delims);
        size_t idx = base + bit;
        uint64_t bitmask = 1ULL << bit;
        all_delims = fast_clear_lowest(all_delims);

        if (LIKELY(bitmask & commas)) {
          scanner->scanned_length = idx;
          if (UNLIKELY(cross_block_cell)) {
            if (quote_char > 0)
              fast_set_quote_flags(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
            cell_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
            cross_block_cell = 0;
          } else
            fast_store_cell(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
          scanner->cell_start = idx + 1;
        } else if (bitmask & crs) {
          if (UNLIKELY(cross_block_cell)) {
            FAST_ROWEND_QUOTED(scanner, buff, idx, 1, quote_char);
            cross_block_cell = 0;
          } else
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 1);
        } else {
          if (UNLIKELY(cross_block_cell)) {
            FAST_ROWEND_QUOTED(scanner, buff, idx, 0, quote_char);
            cross_block_cell = 0;
          } else
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 0);
        }
      }
      continue;
    }

    /*
     * Quote-aware path.
     */
    if (UNLIKELY(malformed_quoting)) {
      i = fast_process_block_malformed(scanner, buff, bytes_read, i, &inside_quote, quote_char, delimiter, 0);
      if (VERY_UNLIKELY(i == (size_t)-1))
        return scanner->abort ? zsv_status_cancelled : zsv_status_max_rows_read;
      continue;
    }

    /* Standard quoting: use prefix-XOR to compute state_mask.
     * Every quote toggles the inside/outside state.
     * PCLMULQDQ on x86 reduces this to a single instruction. */
    {
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

      /* Standard CSV: store cells raw (zero-copy passthrough).
       * Prefix-XOR already identified correct delimiter positions,
       * so cell data from cell_start to idx is the raw field content
       * including any original quoting. The writer outputs it as-is,
       * avoiding the normalize-then-re-quote overhead. */
      while (valid_delims) {
        int bit = __builtin_ctzll(valid_delims);
        size_t idx = base + bit;
        uint64_t bitmask = 1ULL << bit;
        valid_delims = fast_clear_lowest(valid_delims);

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
      } else if (malformed_quoting ? (i == scanner->cell_start) : 1) {
        inside_quote = 1;
      }
      continue;
    }

    if (inside_quote)
      continue;

    if (c == delimiter) {
      scanner->scanned_length = i;
      if (malformed_quoting) {
        if (quote_char > 0)
          fast_set_quote_flags(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
      } else {
        fast_store_cell(scanner, buff + scanner->cell_start, i - scanner->cell_start);
      }
      scanner->cell_start = i + 1;
    } else if (c == '\r') {
      if (malformed_quoting) {
        FAST_ROWEND_QUOTED(scanner, buff, i, 1, quote_char);
      } else {
        FAST_ROWEND_NOQUOTE(scanner, buff, i, 1);
      }
    } else if (c == '\n') {
      if (malformed_quoting) {
        FAST_ROWEND_QUOTED(scanner, buff, i, 0, quote_char);
      } else {
        FAST_ROWEND_NOQUOTE(scanner, buff, i, 0);
      }
    }
  }

  /* Carry quote state across buffer boundaries */
  if (inside_quote)
    scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
  else
    scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;

  /* If there's remaining cell data (no trailing row-end), set quote
   * flags so zsv_finish can properly normalize the final cell.
   * Only needed when malformed_quoting is active (cell_dl normalization). */
  if (malformed_quoting && quote_char > 0 && i > scanner->cell_start && !inside_quote)
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
