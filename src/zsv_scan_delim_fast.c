/*
 * Branchless fast CSV scanner.
 *
 * Uses 64-byte SIMD processing to find delimiters and row-ends in bulk,
 * then calls cell_dl() / cell_and_row_dl() to store cells using the
 * existing infrastructure.
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

static enum zsv_status zsv_scan_delim_fast(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
  /* Guard: fall back for unsupported configurations */
  if (scanner->opts.no_quotes == 0        /* quotes enabled — not yet supported */
      || scanner->opts.delimiter != ','   /* non-comma delimiter — not yet supported */
#ifndef ZSV_NO_ONLY_CRLF
      || scanner->opts.only_crlf_rowend   /* only-crlf mode — not yet supported */
#endif
      || scanner->opts.malformed_utf8_replace /* UTF-8 replacement — not yet supported */
      || scanner->opts.cell_handler       /* per-cell callback — not yet supported */
      ) {
    return zsv_scan_delim(scanner, buff, bytes_read);
  }

  size_t i = scanner->partial_row_length;
  bytes_read += i;
  scanner->partial_row_length = 0;
  scanner->buffer_end = bytes_read;

  uint8x16_t v_comma = vdupq_n_u8(',');
  uint8x16_t v_nl = vdupq_n_u8('\n');
  uint8x16_t v_cr = vdupq_n_u8('\r');

  /* Process 64 bytes at a time */
  while (i + 64 <= bytes_read) {
    uint64_t commas = 0, newlines = 0, crs = 0;
    for (int chunk = 0; chunk < 4; chunk++) {
      uint8x16_t b = vld1q_u8(buff + i + chunk * 16);
      unsigned shift = chunk * 16;
      commas   |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_comma)) << shift;
      newlines |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_nl)) << shift;
      crs      |= (uint64_t)fast_neon_movemask(vceqq_u8(b, v_cr)) << shift;
    }

    uint64_t all = commas | newlines | crs;
    if (LIKELY(all == 0)) {
      i += 64;
      continue;
    }

    size_t base = i;
    i += 64;

    while (all) {
      int bit = __builtin_ctzll(all);
      size_t idx = base + bit;
      uint64_t bitmask = 1ULL << bit;
      all &= (all - 1);

      if (LIKELY(bitmask & commas)) {
        scanner->scanned_length = idx;
        cell_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
        scanner->cell_start = idx + 1;
      } else if (bitmask & crs) {
        FAST_HANDLE_ROWEND(scanner, buff, idx, 1);
      } else {
        /* \n */
        FAST_HANDLE_ROWEND(scanner, buff, idx, 0);
      }
    }
  }

  /* Scalar tail */
  for (; i < bytes_read; i++) {
    unsigned char c = buff[i];
    if (c == ',') {
      scanner->scanned_length = i;
      cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
      scanner->cell_start = i + 1;
    } else if (c == '\r') {
      FAST_HANDLE_ROWEND(scanner, buff, i, 1);
    } else if (c == '\n') {
      FAST_HANDLE_ROWEND(scanner, buff, i, 0);
    }
  }

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
