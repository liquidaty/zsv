// #define DISABLE_CELL_OUTPUT 1
// #define DISABLE_FAST 1

#ifdef ZSV_SUPPORT_PULL_PARSER

#define zsv_internal_save_reg(x) scanner->pull.regs->delim.x = x
#define zsv_internal_save_regs(loc)                                                                                    \
  do {                                                                                                                 \
    scanner->pull.regs->delim.location = loc;                                                                          \
    scanner->pull.buff = buff;                                                                                         \
    scanner->pull.bytes_read = bytes_read;                                                                             \
    zsv_internal_save_reg(i);                                                                                          \
    zsv_internal_save_reg(bytes_chunk_end);                                                                            \
    zsv_internal_save_reg(bytes_read);                                                                                 \
    zsv_internal_save_reg(delimiter);                                                                                  \
    zsv_internal_save_reg(c);                                                                                          \
    zsv_internal_save_reg(skip_next_delim);                                                                            \
    zsv_internal_save_reg(quote);                                                                                      \
    zsv_internal_save_reg(mask_total_offset);                                                                          \
    zsv_internal_save_reg(mask);                                                                                       \
    zsv_internal_save_reg(mask_last_start);                                                                            \
  } while (0)

#define zsv_internal_restore_reg(x) x = scanner->pull.regs->delim.x
#define zsv_internal_restore_regs()                                                                                    \
  do {                                                                                                                 \
    buff = scanner->pull.buff;                                                                                         \
    bytes_read = scanner->pull.bytes_read;                                                                             \
    zsv_internal_restore_reg(i);                                                                                       \
    zsv_internal_restore_reg(bytes_chunk_end);                                                                         \
    zsv_internal_restore_reg(bytes_read);                                                                              \
    zsv_internal_restore_reg(delimiter);                                                                               \
    zsv_internal_restore_reg(c);                                                                                       \
    zsv_internal_restore_reg(skip_next_delim);                                                                         \
    zsv_internal_restore_reg(quote);                                                                                   \
    zsv_internal_restore_reg(mask_total_offset);                                                                       \
    zsv_internal_restore_reg(mask);                                                                                    \
    zsv_internal_restore_reg(mask_last_start);                                                                         \
    memset(&v.dl, scanner->opts.delimiter, sizeof(zsv_uc_vector));                                                     \
    memset(&v.nl, '\n', sizeof(zsv_uc_vector));                                                                        \
    memset(&v.cr, '\r', sizeof(zsv_uc_vector));                                                                        \
    memset(&v.qt, scanner->opts.no_quotes > 0 ? 0 : '"', sizeof(v.qt));                                                \
  } while (0)
#endif

#define CONCAT_DEFS_INNER(a, b) a##b
#define CONCAT_DEFS(a, b) CONCAT_DEFS_INNER(a, b)
#define ZSV_SCAN_DELIM_SLOW_PATH CONCAT_DEFS(ZSV_SCAN_DELIM, _slow)
#ifndef ZSV_SUPPORT_PULL_PARSER
#define ZSV_SCAN_DELIM_FAST_PATH CONCAT_DEFS(ZSV_SCAN_DELIM, _fast)
#define ZSV_SCAN_DELIM_FAST_PATH_PROCESS_CHAR CONCAT_DEFS(ZSV_SCAN_DELIM_FAST_PATH, _process_char)
#endif

static enum zsv_status ZSV_SCAN_DELIM_SLOW_PATH(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  struct {
    zsv_uc_vector dl;
    zsv_uc_vector nl;
    zsv_uc_vector cr;
    zsv_uc_vector qt;
  } v;

  size_t i;
  size_t bytes_chunk_end;
  char delimiter;
  unsigned char c;
  char skip_next_delim;
  int quote;
  size_t mask_total_offset;
  zsv_mask_t mask;
  int mask_last_start;

#ifdef ZSV_SUPPORT_PULL_PARSER
  if (scanner->pull.regs->delim.location) {
    zsv_internal_restore_regs();
    if (scanner->pull.regs->delim.location == 1)
      goto zsv_cell_and_row_dl_1;
    goto zsv_cell_and_row_dl_2;
  }
#endif
  bytes_read += scanner->partial_row_length;
  i = scanner->partial_row_length;
  skip_next_delim = 0;
  bytes_chunk_end = bytes_read >= sizeof(zsv_uc_vector) ? bytes_read - sizeof(zsv_uc_vector) + 1 : 0;
  delimiter = scanner->opts.delimiter;
  scanner->partial_row_length = 0;

  // to do: move into one-time execution code?
  // (but, will also locate away from function stack)
  quote = scanner->opts.no_quotes > 0 ? -1 : '"';  // ascii code 34
  memset(&v.dl, delimiter, sizeof(zsv_uc_vector)); // ascii code 44
  memset(&v.nl, '\n', sizeof(zsv_uc_vector));      // ascii code 10
  memset(&v.cr, '\r', sizeof(zsv_uc_vector));      // ascii code 13
  memset(&v.qt, scanner->opts.no_quotes > 0 ? 0 : '"', sizeof(v.qt));

  if (scanner->quoted & ZSV_PARSER_QUOTE_PENDING) {
    // if we're here, then the last chunk we read ended with a lone quote char inside
    // a quoted cell, and we are waiting to find out whether it is followed by
    // another dbl-quote e.g. if the end of the last chunk is |, we had:
    //    ...,"hel"|"o"
    //    ...,"hel"|,...
    //    ...,"hel"|p,...
    scanner->quoted -= ZSV_PARSER_QUOTE_PENDING;
    if (buff[i] != quote) {
      scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
      scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED; // scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;
      scanner->quote_close_position = i - scanner->cell_start - 1;
    } else {
      scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
      scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
      i++;
    }
  }

#define scanner_last (i ? buff[i - 1] : scanner->last)

  mask_total_offset = 0;
  mask = 0;
  scanner->buffer_end = bytes_read;
  for (; i < bytes_read; i++) {
    if (UNLIKELY(mask == 0)) {
      mask_last_start = i;
      if (VERY_LIKELY(i < bytes_chunk_end)) {
        // keep going until we get a delim or we are at the eof
        mask_total_offset = vec_delims(buff + i, bytes_read - i, &v.dl, &v.nl, &v.cr, &v.qt, &mask);
        if (LIKELY(mask_total_offset != 0)) {
          i += mask_total_offset;
          if (VERY_UNLIKELY(mask == 0 && i == bytes_read))
            break; // vector processing ended on exactly our buffer end
        }
      } else if (skip_next_delim) {
        skip_next_delim = 0;
        continue;
      }
    }
    if (VERY_LIKELY(mask)) {
      size_t next_offset = NEXT_BIT(mask);
      i = mask_last_start + next_offset - 1;
      mask = clear_lowest_bit(mask);
      if (VERY_UNLIKELY(skip_next_delim)) {
        skip_next_delim = 0;
        continue;
      }
    }

    // to do: consolidate csv and tsv/scanner->delimiter parsers
    c = buff[i];
    if (LIKELY(c == delimiter)) { // case ',':
      if ((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        scanner->scanned_length = i;
#ifndef DISABLE_CELL_OUTPUT
        cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
#endif
        scanner->cell_start = i + 1;
        c = 0;
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if (UNLIKELY(c == '\r')) {
#ifndef ZSV_NO_ONLY_CRLF
      if (VERY_UNLIKELY(scanner->opts.only_crlf_rowend)) {
        if (scanner->quoted & ZSV_PARSER_QUOTE_PENDING_LF)
          // if we already had a lone \r in this cell,
          // flip the flag to ZSV_PARSER_QUOTE_NEEDED
          scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
        else
          // otherwise this is the first \r in this cell,
          // so set ZSV_PARSER_QUOTE_PENDING_LF, which
          // will be removed if the next char is LF
          scanner->quoted |= ZSV_PARSER_QUOTE_PENDING_LF;
        continue;
      }
#endif
      if ((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        scanner->scanned_length = i;
#ifndef DISABLE_CELL_OUTPUT
        enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        if (VERY_UNLIKELY(stat))
          return stat;
#endif
#ifdef ZSV_SUPPORT_PULL_PARSER
        if (scanner->pull.now) {
          scanner->pull.now = 0;
          scanner->row.used = scanner->pull.row_used;
          zsv_internal_save_regs(1);
          return zsv_status_row;
        }
      zsv_cell_and_row_dl_1:
        scanner->row.used = 0;
        scanner->pull.regs->delim.location = 0;
#endif
        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
        scanner->data_row_count++;
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if (UNLIKELY(c == '\n')) {
      if ((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        int is_crlf = (scanner_last == '\r');

        // Handle logic for when we should SKIP this char (not a row end)
#ifndef ZSV_NO_ONLY_CRLF
        if (VERY_UNLIKELY(scanner->opts.only_crlf_rowend)) {
          if (!is_crlf) {
            scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
            continue; // only-crlf mode: ignore lone \n
          } else
            // remove ZSV_PARSER_QUOTE_PENDING_LF if we have it
            scanner->quoted &= ~ZSV_PARSER_QUOTE_PENDING_LF;
        } else
#endif
        {
          if (is_crlf) {
            // Standard mode: ignore \n because \r already handled the row end
            scanner->cell_start = i + 1;
            scanner->row_start = i + 1;
            continue;
          }
        }

        // If we reached here, this is a row end
        scanner->scanned_length = i;

        // Calculate cell length. In only-crlf mode, we must exclude the preceding \r
        size_t cell_len = i - scanner->cell_start;
#ifndef ZSV_NO_ONLY_CRLF
        if (VERY_UNLIKELY(scanner->opts.only_crlf_rowend))
          cell_len--;
#endif
#ifndef DISABLE_CELL_OUTPUT
        enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, cell_len);
        if (VERY_UNLIKELY(stat))
          return stat;
#endif
#ifdef ZSV_SUPPORT_PULL_PARSER
        if (scanner->pull.now) {
          scanner->pull.now = 0;
          scanner->row.used = scanner->pull.row_used;
          zsv_internal_save_regs(2);
          return zsv_status_row;
        }
      zsv_cell_and_row_dl_2:
        scanner->row.used = 0;
        scanner->pull.regs->delim.location = 0;
#endif
        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
        scanner->data_row_count++;
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if (LIKELY(c == quote)) {
      if (i == scanner->cell_start && !scanner->buffer_exceeded) {
        scanner->quoted = ZSV_PARSER_QUOTE_UNCLOSED;
        scanner->quote_close_position = 0;
        c = 0;
      } else if (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) {
        // the cell started with a quote that is not yet closed
        if (VERY_LIKELY(i + 1 < bytes_read)) {
          if (LIKELY(buff[i + 1] != quote)) {
            // buff[i] is the closing quote (not an escaped quote)
            scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
            scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;

            // keep track of closing quote position to handle the edge case
            // where content follows the closing quote e.g. cell content is:
            //   "this-cell"-did-not-need-quotes
            if (LIKELY(scanner->quote_close_position == 0))
              scanner->quote_close_position = i - scanner->cell_start;
          } else {
            // next char is also '"'
            // e.g. cell content is: "this "" is a dbl quote"
            //            cursor is here => ^
            // include in cell content and don't further process
            scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
            scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
            skip_next_delim = 1;
          }
        } else // we are at the end of this input chunk
          scanner->quoted |= ZSV_PARSER_QUOTE_PENDING;
      } else {
        // cell_length > 0 and cell did not start w quote, so
        // we have a quote in middle of an unquoted cell
        // process as a normal char
        scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
        scanner->quote_close_position = scanner->quoted & ZSV_PARSER_QUOTE_CLOSED ? scanner->quote_close_position : 0;
      }
    }
  }
  scanner->scanned_length = i;

  // save bytes_read-- we will need to shift any remaining partial row
  // before we read next from our input. however, we intentionally refrain
  // from doing this until the next parse_more() call, so that the entirety
  // of all rows parsed thus far are still available until that next call
  scanner->old_bytes_read = bytes_read;

  return zsv_status_ok;
}

#ifndef ZSV_SUPPORT_PULL_PARSER
#if defined(__aarch64__)
#include <arm_neon.h>

#ifndef NEON_MOVEMASK_64_DEFINED
#define NEON_MOVEMASK_64_DEFINED
/**
 * Extract an 8-bit mask from NEON registers based on a target character.
 * This simulates the behavior of _mm_movemask_epi8 for ARM architecture.
 */
static inline uint64_t neon_movemask_64(uint8x16_t b0, uint8x16_t b1, uint8x16_t b2, uint8x16_t b3, uint8x16_t v_c,
                                        uint8x16_t bit_weights) {

  uint8x16_t m0 = vandq_u8(vceqq_u8(b0, v_c), bit_weights);
  uint8x16_t m1 = vandq_u8(vceqq_u8(b1, v_c), bit_weights);
  uint8x16_t m2 = vandq_u8(vceqq_u8(b2, v_c), bit_weights);
  uint8x16_t m3 = vandq_u8(vceqq_u8(b3, v_c), bit_weights);

  return (uint64_t)vaddv_u8(vget_low_u8(m0)) | ((uint64_t)vaddv_u8(vget_high_u8(m0)) << 8) |
         ((uint64_t)vaddv_u8(vget_low_u8(m1)) << 16) | ((uint64_t)vaddv_u8(vget_high_u8(m1)) << 24) |
         ((uint64_t)vaddv_u8(vget_low_u8(m2)) << 32) | ((uint64_t)vaddv_u8(vget_high_u8(m2)) << 40) |
         ((uint64_t)vaddv_u8(vget_low_u8(m3)) << 48) | ((uint64_t)vaddv_u8(vget_high_u8(m3)) << 56);
}
#endif

/**
 * Character-by-character processing used by the tail and the "relevant" bits found by SIMD.
 * Manages the scanner state machine, including row/cell boundaries and quoted fields.
 */
static inline enum zsv_status ZSV_SCAN_DELIM_FAST_PATH_PROCESS_CHAR(struct zsv_scanner *scanner, char delimiter,
                                                                    char quote, unsigned char *buff, size_t bytes_read,
                                                                    size_t idx, char *skip_next_delim) {
  unsigned char c = buff[idx];

  // Handle escaped quotes: skip the second quote in a "" sequence
  if (VERY_UNLIKELY(*skip_next_delim)) {
    *skip_next_delim = 0;
    return zsv_status_ok;
  }

  if (LIKELY(c == delimiter)) {
    if ((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
      scanner->scanned_length = idx;
#ifndef DISABLE_CELL_OUTPUT
      cell_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
#endif
      scanner->cell_start = idx + 1;
    } else {
      scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    }
  } else if (c == '\r' || c == '\n') {
    // Handle row endings. Guard CRLF sequences from double-triggering row ends
    if (c == '\n' && (idx > 0 ? buff[idx - 1] : scanner->last) == '\r' &&
        (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
      scanner->cell_start = idx + 1;
      scanner->row_start = idx + 1;
    } else {
      if ((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        scanner->scanned_length = idx;
#ifndef DISABLE_CELL_OUTPUT
        enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
        if (VERY_UNLIKELY(stat))
          return stat;
#endif
        scanner->cell_start = idx + 1;
        scanner->row_start = idx + 1;
        scanner->data_row_count++;
      } else {
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
      }
    }
  } else if (LIKELY(c == quote)) {
    // Structural logic for opening and closing quoted fields
    if (idx == scanner->cell_start && !scanner->buffer_exceeded) {
      scanner->quoted = ZSV_PARSER_QUOTE_UNCLOSED;
      scanner->quote_close_position = 0;
    } else if (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) {
      if (VERY_LIKELY(idx + 1 < bytes_read)) {
        if (LIKELY(buff[idx + 1] != quote)) {
          // Found closing quote
          scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
          scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;
          if (LIKELY(scanner->quote_close_position == 0))
            scanner->quote_close_position = idx - scanner->cell_start;
        } else {
          // Found escaped quote ("")
          scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
          scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
          (*skip_next_delim) = 1;
        }
      } else {
        // Quote at end of buffer; check next chunk to see if it's escaped
        scanner->quoted |= ZSV_PARSER_QUOTE_PENDING;
      }
    } else {
      // Literal quote inside unquoted data
      scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
      scanner->quote_close_position = scanner->quoted & ZSV_PARSER_QUOTE_CLOSED ? scanner->quote_close_position : 0;
    }
  }
  return zsv_status_ok;
}

static enum zsv_status ZSV_SCAN_DELIM_FAST_PATH(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  size_t i = scanner->partial_row_length;
  bytes_read += i;
  scanner->partial_row_length = 0;
  char delimiter = scanner->opts.delimiter;
  char skip_next_delim = 0;
  int quote = scanner->opts.no_quotes > 0 ? -1 : '"';

  // 1. Resolve quotes split across buffer boundaries
  if (scanner->quoted & ZSV_PARSER_QUOTE_PENDING) {
    scanner->quoted &= ~ZSV_PARSER_QUOTE_PENDING;
    if (buff[i] != quote) {
      scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
      scanner->quoted &= ~ZSV_PARSER_QUOTE_UNCLOSED;
      scanner->quote_close_position = i - scanner->cell_start - 1;
    } else {
      scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
      scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
      i++;
    }
  }

  // 2. Initialize trackers based on the post-resolve quotes split starting index
  unsigned char prev_c = (i > 0) ? buff[i - 1] : scanner->last;
  uint64_t last_char_was_line_end = (prev_c == '\n' || prev_c == '\r' || (i == 0 && scanner->last == 0));
  int current_inside_quote = (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) ? 1 : 0;

  // 3. Primary SIMD Loop: 64 bytes per iteration

  // Initialize constant vectors once
  const uint8x16_t bit_weights = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
  uint8x16_t v_nl = vdupq_n_u8('\n');
  uint8x16_t v_cr = vdupq_n_u8('\r');
  uint8x16_t v_dl = vdupq_n_u8(delimiter);
  uint8x16_t v_qt = vdupq_n_u8(quote);

  for (; i + 64 <= bytes_read; i += 64) {
    uint8x16_t b0 = vld1q_u8(buff + i), b1 = vld1q_u8(buff + i + 16);
    uint8x16_t b2 = vld1q_u8(buff + i + 32), b3 = vld1q_u8(buff + i + 48);

    uint64_t nl = neon_movemask_64(b0, b1, b2, b3, v_nl, bit_weights);
    uint64_t qt = neon_movemask_64(b0, b1, b2, b3, v_qt, bit_weights);
    uint64_t dl = neon_movemask_64(b0, b1, b2, b3, v_dl, bit_weights);
    uint64_t cr = neon_movemask_64(b0, b1, b2, b3, v_cr, bit_weights);

    // Filter escaped pairs for the state machine
    uint64_t qt_shifted = (qt << 1) | (scanner->last == quote ? 1ULL : 0ULL);
    uint64_t escaped_pairs = qt & qt_shifted;
    uint64_t all_escaped_bits = escaped_pairs | (escaped_pairs >> 1);
    uint64_t qt_filtered = qt & ~all_escaped_bits;

    uint64_t line_ends = nl | cr;
    uint64_t line_ends_shifted = (line_ends << 1) | (last_char_was_line_end ? 1ULL : 0ULL);

    uint64_t valid_openers = qt_filtered & line_ends_shifted;
    uint64_t A = ~(qt_filtered & ~line_ends_shifted);

    // Prefix-XOR state machine - allows the compiler to
    // schedule these bitwise operations optimally rather than
    // calculate in a for loop
    uint64_t B = valid_openers;
    B ^= (A & (B << 1));
    A &= (A << 1);
    B ^= (A & (B << 2));
    A &= (A << 2);
    B ^= (A & (B << 4));
    A &= (A << 4);
    B ^= (A & (B << 8));
    A &= (A << 8);
    B ^= (A & (B << 16));
    A &= (A << 16);
    B ^= (A & (B << 32));
    A &= (A << 32);
    uint64_t state_mask = (current_inside_quote ? A : 0) ^ B;

    // Stop for delims/newlines outside quotes, and ALL quotes (for PROCESS_CHAR metadata)
    uint64_t relevant = ((dl | nl | cr) & ~state_mask) | qt;
    while (VERY_LIKELY(relevant)) {
      int bit = __builtin_ctzll(relevant);
      size_t idx = i + bit;
      enum zsv_status stat =
        ZSV_SCAN_DELIM_FAST_PATH_PROCESS_CHAR(scanner, delimiter, quote, buff, bytes_read, idx, &skip_next_delim);
      if (VERY_UNLIKELY(stat))
        return stat;
      relevant &= (relevant - 1);
    }

    // Capture the state for next block
    current_inside_quote = (state_mask >> 63) & 1;

    // The "Priority Sync": If PROCESS_CHAR closed the quote, SIMD must respect it
    if (!(scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED))
      current_inside_quote = 0;

    last_char_was_line_end = ((nl | cr) >> 63) & 1;
    scanner->last = buff[i + 63];
  }

  // 4. Final Sync for parallelization and tail
  if (current_inside_quote) {
    scanner->quoted |= ZSV_PARSER_QUOTE_UNCLOSED;
    scanner->quote_close_position = 0;
  }

  // 5. Scalar Tail
  for (; i < bytes_read; i++) {
    enum zsv_status stat =
      ZSV_SCAN_DELIM_FAST_PATH_PROCESS_CHAR(scanner, delimiter, quote, buff, bytes_read, i, &skip_next_delim);
    if (VERY_UNLIKELY(stat))
      return stat;
    scanner->last = buff[i];
  }

  scanner->scanned_length = i;
  scanner->old_bytes_read = bytes_read;
  return zsv_status_ok;
}
#endif // #if defined(__aarch64__)
#endif // ifndef ZSV_SUPPORT_PULL_PARSER

static enum zsv_status ZSV_SCAN_DELIM(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {

#ifndef DISABLE_FAST
#if defined(ZSV_SUPPORT_PULL_PARSER) || !defined(__aarch64__)
  // Use slow path
  return ZSV_SCAN_DELIM_SLOW_PATH(scanner, buff, bytes_read);
#else
#ifndef ZSV_NO_ONLY_CRLF
  if (scanner->opts.only_crlf_rowend)
    return ZSV_SCAN_DELIM_SLOW_PATH(scanner, buff, bytes_read);
#endif
  // Use fast
  return ZSV_SCAN_DELIM_FAST_PATH(scanner, buff, bytes_read);
#endif
#else
  return ZSV_SCAN_DELIM_SLOW_PATH(scanner, buff, bytes_read);
#endif
}