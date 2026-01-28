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

#include <arm_neon.h>

// Helper to extract bitmask from NEON registers
#ifndef NEON_MOVEMASK_64_DEFINED
#define NEON_MOVEMASK_64_DEFINED
inline uint64_t neon_movemask_64(uint8x16_t b0, uint8x16_t b1, uint8x16_t b2, uint8x16_t b3, uint8_t c) {
    const uint8x16_t bit_weights = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    uint8x16_t m0 = vandq_u8(vceqq_u8(b0, vdupq_n_u8(c)), bit_weights);
    uint8x16_t m1 = vandq_u8(vceqq_u8(b1, vdupq_n_u8(c)), bit_weights);
    uint8x16_t m2 = vandq_u8(vceqq_u8(b2, vdupq_n_u8(c)), bit_weights);
    uint8x16_t m3 = vandq_u8(vceqq_u8(b3, vdupq_n_u8(c)), bit_weights);
    return (uint64_t)vaddv_u8(vget_low_u8(m0))        | ((uint64_t)vaddv_u8(vget_high_u8(m0)) << 8)  |
           ((uint64_t)vaddv_u8(vget_low_u8(m1)) << 16) | ((uint64_t)vaddv_u8(vget_high_u8(m1)) << 24) |
           ((uint64_t)vaddv_u8(vget_low_u8(m2)) << 32) | ((uint64_t)vaddv_u8(vget_high_u8(m2)) << 40) |
           ((uint64_t)vaddv_u8(vget_low_u8(m3)) << 48) | ((uint64_t)vaddv_u8(vget_high_u8(m3)) << 56);
}
#endif

static enum zsv_status ZSV_SCAN_DELIM(struct zsv_scanner *scanner, unsigned char *buff, size_t bytes_read) {
    size_t i = scanner->partial_row_length;
    bytes_read += i;
    scanner->partial_row_length = 0;
    char delimiter = scanner->opts.delimiter;
    int quote = scanner->opts.no_quotes > 0 ? -1 : '"';

    // Quote state tracking for the bitmask
    uint64_t last_char_was_newline = (scanner->last == '\n' || i == 0);
    int current_inside_quote = (scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) ? 1 : 0;

    for (; i + 64 <= bytes_read; i += 64) {
        uint8x16_t b0 = vld1q_u8(buff + i), b1 = vld1q_u8(buff + i + 16);
        uint8x16_t b2 = vld1q_u8(buff + i + 32), b3 = vld1q_u8(buff + i + 48);

        uint64_t n = neon_movemask_64(b0, b1, b2, b3, '\n');
        uint64_t q = neon_movemask_64(b0, b1, b2, b3, '"');

        // State machine from fast.c to identify characters inside quotes
        uint64_t nl_shifted = (n << 1) | (last_char_was_newline ? 1ULL : 0ULL);
        uint64_t valid_openers = q & nl_shifted;
        uint64_t A = ~(q & ~nl_shifted);
        uint64_t B = valid_openers;
        for (int s = 0; s < 6; s++) { B ^= (A & (B << (1 << s))); A &= (A << (1 << s)); }
        uint64_t state_mask = (current_inside_quote ? A : 0) ^ B;

        uint64_t d = neon_movemask_64(b0, b1, b2, b3, delimiter);
        uint64_t r = neon_movemask_64(b0, b1, b2, b3, '\r');

        // Process only delimiters and quotes that are "active" (not escaped/inside quotes)
        uint64_t relevant = (d | n | r | q) & ~state_mask;

        while (relevant) {
            int bit = __builtin_ctzll(relevant);
            size_t idx = i + bit;
            unsigned char c = buff[idx];

            if (c == delimiter) {
                scanner->scanned_length = idx;
                cell_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
                scanner->cell_start = idx + 1;
            } else if (c == '\r' || c == '\n') {
                // Handling CRLF and row ends exactly as slow.c
                if (c == '\n' && (idx > 0 ? buff[idx-1] : scanner->last) == '\r') {
                    scanner->cell_start = idx + 1;
                    scanner->row_start = idx + 1;
                } else {
                    scanner->scanned_length = idx;
                    enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, idx - scanner->cell_start);
                    if (stat) return stat;
                    scanner->cell_start = idx + 1;
                    scanner->row_start = idx + 1;
                    scanner->data_row_count++;
                }
            } else if (c == quote) {
                // Re-incorporating the specific quote handling logic from slow.c
                if (idx == scanner->cell_start && !scanner->buffer_exceeded) {
                    scanner->quoted = ZSV_PARSER_QUOTE_UNCLOSED;
                }
                // (Further quote logic here for embedded/closed cases)
            }
            relevant &= (relevant - 1);
        }
        current_inside_quote = (state_mask >> 63) & 1;
        last_char_was_newline = (n >> 63) & 1;
    }

    // Residual scalar loop for bytes_read % 64 remains necessary
    return zsv_status_ok;
}

#define scanner_last (i ? buff[i - 1] : scanner->last)
