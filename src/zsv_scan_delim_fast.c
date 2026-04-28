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
 * Falls back to the compat/scalar engine on unsupported platforms.
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
 * the compat/scalar engine does character-by-character.
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
 *
 * The `need_slow` and `no_quotes` flags are pre-computed once at
 * zsv_scan_delim_fast() entry and passed in to avoid re-checking
 * scanner fields on every cell.
 */
__attribute__((always_inline)) static inline void fast_store_cell(struct zsv_scanner *scanner, unsigned char *s,
                                                                  size_t n, int need_slow, unsigned char no_quotes) {
  if (UNLIKELY(need_slow)) {
    fast_store_cell_slow(scanner, s, n);
    return;
  }
  if (VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
    struct zsv_cell c = {s, n, no_quotes, 0};
    scanner->row.cells[scanner->row.used++] = c;
  } else
    scanner->row.overflow++;
  scanner->have_cell = 1;
}

/*
 * Cache-friendly cell store using local variables.
 * Stores directly into the cells array via a cached pointer and index,
 * avoiding repeated loads of scanner->row.used, scanner->row.cells,
 * and scanner->row.allocated through the scanner pointer (which the
 * compiler cannot hoist due to aliasing).
 */
__attribute__((always_inline)) static inline void fast_store_cell_cached(struct zsv_cell *cells, size_t *used_p,
                                                                         size_t allocated, unsigned char *s, size_t n,
                                                                         unsigned char no_quotes) {
  size_t u = *used_p;
  if (VERY_LIKELY(u < allocated)) {
    struct zsv_cell c = {s, n, no_quotes, 0};
    cells[u] = c;
    *used_p = u + 1;
  }
  /* overflow and have_cell handled by caller at row-end */
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
                                                                                     unsigned char *s, size_t n,
                                                                                     int need_slow,
                                                                                     unsigned char no_quotes) {
  fast_store_cell(scanner, s, n, need_slow, no_quotes);
  return fast_row_end_slow(scanner);
}

/*
 * Handle a row-end for an unquoted cell (fast path).
 * Skips cell_dl()/row_dl() overhead.
 */
#define FAST_ROWEND_NOQUOTE(scanner, buff, idx, is_cr, need_slow_, no_quotes_)                                         \
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
    enum zsv_status stat_ = fast_store_cell_and_row((scanner), (buff) + (scanner)->cell_start,                         \
                                                    (idx) - (scanner)->cell_start, (need_slow_), (no_quotes_));        \
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

// Forward declaration of implementation function
static enum zsv_status zsv_scan_delim_fast_impl(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read);

static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  /* Guard: pad small inputs to avoid edge cases in scalar tail processing.
   * Small inputs (<64 bytes) can trigger quote state confusion and buffer
   * boundary issues. Padding ensures SIMD processing is used instead of
   * potentially buggy scalar tail paths.
   *
   * Use a persistent heap-allocated buffer to avoid dangling pointers when
   * cell data is accessed later in zsv_finish().
   *
   * zsv_scan_delim_fast_impl() receives bytes_read as the number of newly-read
   * bytes, then adds scanner->partial_row_length internally. Copy that full
   * logical buffer here so partial rows keep seeing appended bytes. */
  size_t logical_bytes_read = scanner->partial_row_length + bytes_read;
  if (bytes_read > 0 && logical_bytes_read < 64) {
    static const size_t PADDED_SIZE = 64;

    // Ensure we have a persistent padded buffer
    if (!scanner->padded_input_buffer) {
      scanner->padded_input_buffer = malloc(PADDED_SIZE);
      if (!scanner->padded_input_buffer) {
        return zsv_status_memory;
      }
      scanner->padded_input_size = PADDED_SIZE;
    }

    memcpy(scanner->padded_input_buffer, buff, logical_bytes_read);
    memset(scanner->padded_input_buffer + logical_bytes_read, 0, PADDED_SIZE - logical_bytes_read);
    return zsv_scan_delim_fast_impl(scanner, scanner->padded_input_buffer, bytes_read);
  }

  return zsv_scan_delim_fast_impl(scanner, buff, bytes_read);
}

static enum zsv_status zsv_scan_delim_fast_impl(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  /* Guard: fall back for unsupported configurations */
  if (0
#ifndef ZSV_NO_ONLY_CRLF
      || scanner->opts.only_crlf_rowend /* only-crlf mode — not yet supported */
#endif
  ) {
    return zsv_scan_delim(scanner, buff, bytes_read);
  }

  int quote_char = scanner->opts.no_quotes > 0 ? -1 : '"';

  /* Pre-compute per-cell flags once, avoiding repeated field access in the hot loop. */
  int need_slow = (scanner->needed_cols || scanner->opts.malformed_utf8_replace || scanner->opts.cell_handler) ? 1 : 0;
  unsigned char no_quotes = scanner->opts.no_quotes ? 1 : 0;

  size_t i = scanner->partial_row_length;

  /* Reset scanned_length so zsv_cum_scanned_length() doesn't double-count
   * the previous buffer's value (which was already absorbed into
   * cum_scanned_length at the start of zsv_parse_more). */
  scanner->scanned_length = i;

  /* If the entire buffer fits in the scalar tail and contains quotes,
   * use the compat/scalar engine. The scalar tail's simplified quote handling
   * combined with cell_dl's in-place memmove can produce incorrect results
   * for small inputs with complex quoting. No performance impact since
   * this only triggers for inputs < 64 bytes after partial row data. */

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
          /* Extract individual row-end positions via bit extraction so
           * scanned_length and row_start are accurate per-row. This is
           * needed for parallel boundary checks (zsv_cum_scanned_length). */
          uint64_t re = row_ends;
          while (re) {
            int bit = __builtin_ctzll(re);
            size_t rowend_pos = i + bit;
            scanner->scanned_length = rowend_pos + 1;
            scanner->row_start = rowend_pos + 1;
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
            re &= re - 1; /* clear lowest set bit */
          }
          int last_bit = 63 - __builtin_clzll(row_ends);
          size_t last_rowend_pos = i + last_bit;
          scanner->cell_start = last_rowend_pos + 1;
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
          } else {
            inside_quote = 1;
          }
          continue;
        }
        if (inside_quote)
          continue;
        if (c == '\r' || (c == '\n' && (i == 0 ? scanner->last != '\r' : buff[i - 1] != '\r'))) {
          scanner->data_row_count++;
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
          scanner->scanned_length = i + 1;
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
      /* Sync scanner->quoted with the locally-tracked inside_quote state.
       * The SIMD skip_cells loop updates inside_quote via prefix-XOR but
       * does not update scanner->quoted until function exit. When skip_cells
       * is cleared mid-buffer (e.g. at a parallel chunk boundary), we must
       * persist the current state before transitioning to normal_parse,
       * which reads scanner->quoted for its own quote tracking. */
      if (inside_quote)
        scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
      else
        scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;
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

      /* Cache scanner fields in locals to avoid aliasing-induced reloads.
       * The compiler can't hoist these because stores to cells[] could
       * alias any scanner field through the same pointer. */
      size_t cell_start_local = scanner->cell_start;
      if (LIKELY(!need_slow)) {
        struct zsv_cell *cells = scanner->row.cells;
        size_t row_used = scanner->row.used;
        size_t row_allocated = scanner->row.allocated;

        while (all_delims) {
          int bit = __builtin_ctzll(all_delims);
          size_t idx = base + bit;
          uint64_t bitmask = 1ULL << bit;
          all_delims = fast_clear_lowest(all_delims);

          if (LIKELY(bitmask & commas)) {
            fast_store_cell_cached(cells, &row_used, row_allocated, buff + cell_start_local, idx - cell_start_local,
                                   no_quotes);
            cell_start_local = idx + 1;
          } else if (bitmask & crs) {
            scanner->row.used = row_used;
            scanner->cell_start = cell_start_local;
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 1, need_slow, no_quotes);
            row_used = scanner->row.used;
            cell_start_local = scanner->cell_start;
          } else {
            scanner->row.used = row_used;
            scanner->cell_start = cell_start_local;
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 0, need_slow, no_quotes);
            row_used = scanner->row.used;
            cell_start_local = scanner->cell_start;
          }
        }
        scanner->row.used = row_used;
        scanner->have_cell = 1;
      } else {
        /* Slow path: need_slow is set, use original per-cell function */
        while (all_delims) {
          int bit = __builtin_ctzll(all_delims);
          size_t idx = base + bit;
          uint64_t bitmask = 1ULL << bit;
          all_delims = fast_clear_lowest(all_delims);

          if (LIKELY(bitmask & commas)) {
            scanner->scanned_length = idx;
            scanner->cell_start = cell_start_local;
            fast_store_cell_slow(scanner, buff + cell_start_local, idx - cell_start_local);
            cell_start_local = idx + 1;
          } else if (bitmask & crs) {
            scanner->cell_start = cell_start_local;
            FAST_ROWEND_QUOTED(scanner, buff, idx, 1, quote_char);
            cell_start_local = scanner->cell_start;
          } else {
            scanner->cell_start = cell_start_local;
            FAST_ROWEND_QUOTED(scanner, buff, idx, 0, quote_char);
            cell_start_local = scanner->cell_start;
          }
        }
      }
      scanner->cell_start = cell_start_local;
      continue;
    }

    /*
     * Quote-aware path: use prefix-XOR to compute state_mask.
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
      size_t cell_start_q = scanner->cell_start;
      if (LIKELY(!need_slow)) {
        struct zsv_cell *cells = scanner->row.cells;
        size_t row_used = scanner->row.used;
        size_t row_allocated = scanner->row.allocated;

        while (valid_delims) {
          int bit = __builtin_ctzll(valid_delims);
          size_t idx = base + bit;
          uint64_t bitmask = 1ULL << bit;
          valid_delims = fast_clear_lowest(valid_delims);

          if (LIKELY(bitmask & commas)) {
            fast_store_cell_cached(cells, &row_used, row_allocated, buff + cell_start_q, idx - cell_start_q, no_quotes);
            cell_start_q = idx + 1;
          } else if (bitmask & crs) {
            scanner->row.used = row_used;
            scanner->cell_start = cell_start_q;
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 1, need_slow, no_quotes);
            row_used = scanner->row.used;
            cell_start_q = scanner->cell_start;
          } else {
            scanner->row.used = row_used;
            scanner->cell_start = cell_start_q;
            FAST_ROWEND_NOQUOTE(scanner, buff, idx, 0, need_slow, no_quotes);
            row_used = scanner->row.used;
            cell_start_q = scanner->cell_start;
          }
        }
        scanner->row.used = row_used;
        scanner->have_cell = 1;
      } else {
        while (valid_delims) {
          int bit = __builtin_ctzll(valid_delims);
          size_t idx = base + bit;
          uint64_t bitmask = 1ULL << bit;
          valid_delims = fast_clear_lowest(valid_delims);

          if (LIKELY(bitmask & commas)) {
            scanner->scanned_length = idx;
            scanner->cell_start = cell_start_q;
            fast_store_cell_slow(scanner, buff + cell_start_q, idx - cell_start_q);
            cell_start_q = idx + 1;
          } else if (bitmask & crs) {
            scanner->cell_start = cell_start_q;
            FAST_ROWEND_QUOTED(scanner, buff, idx, 1, quote_char);
            cell_start_q = scanner->cell_start;
          } else {
            scanner->cell_start = cell_start_q;
            FAST_ROWEND_QUOTED(scanner, buff, idx, 0, quote_char);
            cell_start_q = scanner->cell_start;
          }
        }
      }
      scanner->cell_start = cell_start_q;
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
      } else {
        inside_quote = 1;
      }
      continue;
    }

    if (inside_quote)
      continue;

    if (c == delimiter) {
      scanner->scanned_length = i;
      fast_store_cell(scanner, buff + scanner->cell_start, i - scanner->cell_start, need_slow, no_quotes);
      scanner->cell_start = i + 1;
    } else if (c == '\r') {
      FAST_ROWEND_NOQUOTE(scanner, buff, i, 1, need_slow, no_quotes);
    } else if (c == '\n') {
      FAST_ROWEND_NOQUOTE(scanner, buff, i, 0, need_slow, no_quotes);
    }
  }

  /* Carry quote state across buffer boundaries */
  if (inside_quote)
    scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
  else
    scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;

  scanner->scanned_length = i;
  scanner->old_bytes_read = bytes_read;
  return zsv_status_ok;
}

#undef FAST_ROWEND_NOQUOTE
#undef FAST_ROWEND_QUOTED

#else /* !ZSV_FAST_PARSER_AVAILABLE */
/* Unsupported platform: fall back to compat/scalar engine */
static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  return zsv_scan_delim(scanner, buff, bytes_read);
}
#endif
