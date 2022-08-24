/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood (self), Matt Wong (Guarnerix Inc dba Liquidaty)
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef ZSV_EXTRAS
#include <time.h>
#endif

#include <zsv/utils/utf8.h>
#include <zsv/utils/compiler.h>

struct zsv_row {
  size_t used, allocated, overflow;
  struct zsv_cell *cells;
};

struct collate_header {
  struct {
    unsigned char *buff;
    size_t used;
  } buff;
  size_t *lengths; // length PLUS 1 of each cell
  size_t column_count;
};

struct zsv_scanner {
  char last;
  struct {
    unsigned char *buff; // provided by caller
    size_t size; // provided by caller
  } buff;

  size_t cell_start;
  unsigned char quoted; // bitfield of ZSV_PARSER_QUOTE_XXX flags

  unsigned char waiting_for_end:1;
  unsigned char checked_bom:1;
  unsigned char free_buff:1;
  unsigned char finished:1;
  unsigned char had_bom:1;
  unsigned char abort:1;
  unsigned char have_cell:1;
  unsigned char _:1;

  size_t quote_close_position;
  struct zsv_opts opts;
//  void (*row_orig)(void *ctx);
//  void (*cell_orig)(void *ctx, unsigned char *, size_t);
//  void *ctx_orig;

  size_t row_start;
  struct zsv_row row;

  size_t scanned_length;
  size_t cum_scanned_length;
  size_t partial_row_length;

  size_t (*read)(void *buff, size_t n, size_t size, void *in);
  void *in;

  size_t (*filter)(void *ctx, unsigned char *buff, size_t bytes_read);
  void *filter_ctx;

  size_t buffer_end;
  size_t old_bytes_read; // only non-zero if we must shift upon next parse_more()

  const char *insert_string;

  size_t empty_header_rows;

  struct zsv_opts opts_orig;

#define ZSV_MODE_DELIM 0
#define ZSV_MODE_FIXED 1
  unsigned char mode;
  struct {
    unsigned *offsets; // 0-based position of each cell end. offset[0] = end of first cell
    unsigned count; // number of offsets
  } fixed;

  struct collate_header *collate_header;

#ifdef ZSV_EXTRAS
  struct {
    size_t cum_row_count; /* total number of rows read */
    time_t last_time;     /* last time from which to check seconds_interval */
    size_t max_rows;      /* max rows to read, including header row(s) */
  } progress;
#endif
};

void collate_header_destroy(struct collate_header **chp) {
  if(*chp) {
    struct collate_header *ch = *chp;
    free(ch->buff.buff);
    free(ch->lengths);
    free(ch);
    *chp = NULL;
  }
}

/* collate_header_append(): return err */
static int collate_header_append(struct zsv_scanner *scanner, struct collate_header **chp) {
  if(!*chp) {
    if((*chp = calloc(1, sizeof(struct collate_header))))
      (*chp)->lengths = calloc(scanner->row.allocated, sizeof(*(*chp)->lengths));
    if(!(*chp) || !(*chp)->lengths) {
      free(*chp);
      fprintf(stderr, "Out of memory!\n");
      return -1;
    }
  }
  struct collate_header *ch = *chp;
  size_t this_row_size = 0;
  size_t column_count = zsv_column_count(scanner);
  for(size_t i = 0, j = column_count; i < j; i++)
    this_row_size += zsv_get_cell(scanner, i).len + 1; // +1: terminating null or delim
  size_t new_row_size = ch->buff.used + this_row_size;
  unsigned char *new_row = realloc(ch->buff.buff, new_row_size);
  if(!new_row) {
    fprintf(stderr, "Out of memory!\n");
    return -1;
  }

  // now: splice the new row into the old row, starting with the last cell
  // e.g. prior row = A1.B1.C1.
  //      this row =  A2.B2.C2.
  //      new_row =   A1.B1.C1..........
  // starting with last cell in this row, move the old data, then splice new:
  //      new_row =   A1.B1.C1.......C2.
  //      new_row =   A1.B1.C1....C1 C2.
  //      new_row =   A1.B1.C1.B2.C1 C2.
  //      new_row =   A1.B1.B1 B2.C1 C2.
  //      new_row =   A1.A2.B1 B2.C1 C2.
  //      new_row =   A1 A2.B1 B2.C1 C2.

  size_t new_row_end = ch->buff.used + this_row_size;
  size_t old_row_end = ch->buff.used;
  ch->buff.used += this_row_size;
  ch->buff.buff = new_row;
  for(size_t i = column_count; i > 0; i--) {
    struct zsv_cell c = zsv_get_cell(scanner, i-1);
    // copy new row's cell value to end
    if(c.len)
      memcpy(new_row + new_row_end - c.len - 1, c.str, c.len);
    new_row[new_row_end - 1] = ' ';
    new_row_end = new_row_end - c.len - 1;

    // move prior cell value
    size_t old_cell_len = ch->lengths[i-1]; // old_cell_len includes delim
    if(old_cell_len) {
      memcpy(new_row + new_row_end - old_cell_len,
             new_row + old_row_end - old_cell_len,
             old_cell_len);
      old_row_end -= old_cell_len;
      new_row_end -= old_cell_len;
    }
    ch->lengths[i-1] += c.len + 1;
  }
  if(column_count > ch->column_count)
    ch->column_count = column_count;
  return 0;
}

__attribute__((always_inline)) static inline void zsv_clear_cell(struct zsv_scanner *scanner) {
  scanner->quoted = 0;
}

__attribute__((always_inline)) static inline void cell_dl(struct zsv_scanner * scanner, unsigned char * s, size_t n, char is_end) {
  // handle quoting
  if(UNLIKELY(scanner->quoted > 0)) {
    if(LIKELY(scanner->quote_close_position + 1 == n)) {
      if(LIKELY((scanner->quoted & ZSV_PARSER_QUOTE_EMBEDDED) == 0)) {
        // this is the easy and usual case: no embedded double-quotes
        // just remove surrounding quotes from content
        s++;
        n -= 2;
      } else { // embedded dbl-quotes to remove
        s++;
        n--;
        // remove dbl-quotes. TO DO: consider adding option to skip this
        for(size_t i = 0; i + 1 < n; i++) {
          if(s[i] == '"' && s[i+1] == '"') {
            if(n > i + 2)
              memmove(s + i + 1, s + i + 2, n - i - 2);
            n--;
          }
        }
        n--;
      }
    } else {
      if(scanner->quote_close_position) {
        // the first char was a quote, and we have content after the closing quote
        // the solution below is a generalized on that will work
        // for the easy and usual case, but by handling separately
        // we avoid the memmove in the easy / usual case
        memmove(s + 1, s, scanner->quote_close_position);
        s += 2;
        n -= 2;
        if(UNLIKELY((scanner->quoted & ZSV_PARSER_QUOTE_EMBEDDED) != 0)) {
          // remove dbl-quotes
          for(size_t i = 0; i + 1 < n; i++) {
            if(s[i] == '"' && s[i+1] == '"') {
              if(n > i + 2)
                memmove(s + i + 1, s + i + 2, n - i - 2);
              n--;
            }
          }
        }
      }
    }
  } else if(UNLIKELY(scanner->opts.delimiter != ',')) {
    if(memchr(s, ',', n))
      scanner->quoted = ZSV_PARSER_QUOTE_NEEDED;
  }
  // end quote handling

  if(VERY_UNLIKELY(scanner->waiting_for_end != 0)) { // overflow: cell size exceeds allocated memory
    if(scanner->opts.overflow)
      scanner->opts.overflow(scanner->opts.ctx, s, n);
  } else {
    if(scanner->opts.cell)
      scanner->opts.cell(scanner->opts.ctx, s, n);
    if(VERY_LIKELY(scanner->row.used < scanner->row.allocated)) {
      struct zsv_row *row = &scanner->row;
      struct zsv_cell c = { s, n, scanner->opts.no_quotes ? 1 : scanner->quoted };
      row->cells[row->used++] = c;
    } else
      scanner->row.overflow++;
  }
  scanner->waiting_for_end = !is_end;
  scanner->have_cell = 1;

  zsv_clear_cell(scanner);
}

__attribute__((always_inline)) static inline enum zsv_status row_dl(struct zsv_scanner *scanner) {
  if(VERY_UNLIKELY(scanner->row.overflow)) {
    fprintf(stderr, "Warning: number of columns (%zu) exceeds row max (%zu)\n",
            scanner->row.allocated + scanner->row.overflow, scanner->row.allocated);
    scanner->row.overflow = 0;
  }
  if(LIKELY(scanner->opts.row != NULL))
    scanner->opts.row(scanner->opts.ctx);
# ifdef ZSV_EXTRAS
  scanner->progress.cum_row_count++;
  if(VERY_UNLIKELY(scanner->opts.progress.rows_interval
                   && scanner->progress.cum_row_count % scanner->opts.progress.rows_interval == 0)) {
    char ok;
    if(!scanner->opts.progress.seconds_interval)
      ok = 1;
    else {
      // using timer_create() would be better, but is not currently supported on
      // all platforms, so the fallback is to poll
      time_t now = time(NULL);
      if(now > scanner->progress.last_time &&
         (unsigned int)(now - scanner->progress.last_time) >=
         scanner->opts.progress.seconds_interval) {
        ok = 1;
        scanner->progress.last_time = now;
      } else
        ok = 0;
    }
    if(ok && scanner->opts.progress.callback)
      scanner->abort = scanner->opts.progress.callback(scanner->opts.progress.ctx, scanner->progress.cum_row_count);
#ifndef NDEBUG
    if(scanner->abort)
      fprintf(stderr, "ZSV parsing aborted at %zu\n", scanner->progress.cum_row_count);
#endif
  }
  if(VERY_UNLIKELY(scanner->progress.max_rows > 0)) {
    if(VERY_UNLIKELY(scanner->progress.cum_row_count == scanner->progress.max_rows)) {
      scanner->abort = 1;
      scanner->row.used = 0;
      return zsv_status_max_rows_read;
    }
  }

# endif
  if(VERY_UNLIKELY(scanner->abort))
    return zsv_status_cancelled;
  scanner->have_cell = 0;
  scanner->row.used = 0;
  return zsv_status_ok;
}

static inline enum zsv_status cell_and_row_dl(struct zsv_scanner *scanner, unsigned char *s, size_t n) {
  cell_dl(scanner, s, n, 1);
  return row_dl(scanner);
}

#ifndef ZSV_NO_AVX
# if !defined(__AVX2__)
#  define ZSV_NO_AVX
# elif defined(HAVE_AVX512)
#  ifndef __AVX512BW__
#   error AVX512 requested, but __AVX512BW__ macro not defined
#  else
#   define VECTOR_BYTES 64
#   define zsv_mask_t uint64_t
#   define movemask_pseudo(x) _mm512_movepi8_mask((__m512i)x)
#   define NEXT_BIT __builtin_ffsl
#  endif
# elif defined(HAVE_AVX256)
#  define VECTOR_BYTES 32
#  define zsv_mask_t uint32_t
#  define movemask_pseudo(x) _mm256_movemask_epi8((__m256i)x)
#  define NEXT_BIT __builtin_ffs
# else
#  define ZSV_NO_AVX
# endif
#endif // ndef ZSV_NO_AVX

#if defined(ZSV_NO_AVX)
# define zsv_mask_t uint16_t
# define VECTOR_BYTES 16
# define NEXT_BIT __builtin_ffs
#endif

typedef unsigned char zsv_uc_vector __attribute__ ((vector_size (VECTOR_BYTES)));

#ifndef ZSV_NO_AVX
# include <immintrin.h>
#else

# if defined(__ARM_NEON) || defined(__ARM_NEON__)
# include <arm_neon.h>
# endif
/*
  provide our own pseudo-movemask, which sets the 1 bit for each corresponding
  non-zero value in the vector (as opposed to real movemask which sets the bit
  only for each corresponding non-zero highest-bit value in the vector)
*/
static inline zsv_mask_t movemask_pseudo(zsv_uc_vector v) {

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  // see https://stackoverflow.com/questions/11870910/
  static const uint8_t __attribute__ ((aligned (16))) _powers[16]=
    { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 };
  uint8x16_t mm_powers = vld1q_u8(_powers);

  // compute the mask from the input
  uint64x2_t imask= vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(vandq_u8(v, mm_powers))));

  // Get the resulting bytes
  uint16_t mask;
  vst1q_lane_u8((uint8_t*)&mask + 0, (uint8x16_t)imask, 0);
  vst1q_lane_u8((uint8_t*)&mask + 1, (uint8x16_t)imask, 8);
  return mask;
#else
  // to do: see https://github.com/WebAssembly/simd/issues/131 for wasm
  zsv_mask_t mask = 0, tmp = 1;
  for(size_t i = 0; i < sizeof(zsv_uc_vector); i++) {
    mask += (v[i] ? tmp : 0);
    tmp <<= 1;
  }
  return mask;
#endif

}
#endif // ZSV_NO_AVX

# include "vector_delim.c"

static enum zsv_status zsv_scan_delim(struct zsv_scanner *scanner,
                                      unsigned char *buff,
                                      size_t bytes_read
                                      ) {
  bytes_read += scanner->partial_row_length;
  size_t i = scanner->partial_row_length;
  unsigned char c;
  char skip_next_delim = 0;
  size_t bytes_chunk_end = bytes_read >= sizeof(zsv_uc_vector) ? bytes_read - sizeof(zsv_uc_vector) + 1 : 0;
  const char delimiter = scanner->opts.delimiter;

  scanner->partial_row_length = 0;

  int quote = scanner->opts.no_quotes > 0 ? -1 : '"'; // ascii code 34
  zsv_uc_vector dl_v; memset(&dl_v, delimiter, sizeof(zsv_uc_vector)); // ascii 44
  zsv_uc_vector nl_v; memset(&nl_v, '\n', sizeof(zsv_uc_vector)); // ascii code 10
  zsv_uc_vector cr_v; memset(&cr_v, '\r', sizeof(zsv_uc_vector)); // ascii code 13
  zsv_uc_vector qt_v; memset(&qt_v, scanner->opts.no_quotes > 0 ? 0 : '"', sizeof(qt_v));

  // case "hel"|"o": check if we have an embedded dbl-quote past the initial opening quote, which was
  // split between the last buffer and this one e.g. "hel""o" where the last buffer ended
  // with "hel" and this one starts with "o"
  if((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED)
     && i > scanner->cell_start + 1 // case "|hello": need the + 1 in case split after first char of quoted value e.g. "hello" => " and hello"
     && scanner->last == quote) {
    if(buff[i] != quote) {
      scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
      scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;
      scanner->quote_close_position = i - scanner->cell_start - 1;
    } else {
      scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
      scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
      i++;
    }
  }

#define scanner_last (i ? buff[i-1] : scanner->last)
  size_t mask_total_offset = 0;
  zsv_mask_t mask = 0;
  int mask_last_start;

  scanner->buffer_end = bytes_read;
  for(; i < bytes_read; i++) {
    if(UNLIKELY(mask == 0)) {
      mask_last_start = i;
      if(VERY_LIKELY(i < bytes_chunk_end)) {
        // keep going until we get a delim or we are at the eof
        mask_total_offset = vec_delims(buff + i, bytes_read - i, &dl_v, &nl_v, &cr_v, &qt_v,
                                       &mask);
        if(LIKELY(mask_total_offset != 0)) {
          i += mask_total_offset;
          if(VERY_UNLIKELY(mask == 0 && i == bytes_read))
            break; // vector processing ended on exactly our buffer end
        }
      } else if(skip_next_delim) {
        skip_next_delim = 0;
        continue;
      }
    }
    if(VERY_LIKELY(mask)) {
      size_t next_offset = NEXT_BIT(mask);
      i = mask_last_start + next_offset - 1;
      mask = clear_lowest_bit(mask);
      if(VERY_UNLIKELY(skip_next_delim)) {
        skip_next_delim = 0;
        continue;
      }
    }

    // to do: consolidate csv and tsv/scanner->delimiter parsers
    c = buff[i];
    if(LIKELY(c == delimiter)) { // case ',':
      if((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        scanner->scanned_length = i;
        cell_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start, 1);
        scanner->cell_start = i + 1;
        c = 0;
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if(UNLIKELY(c == '\r')) {
      if((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        scanner->scanned_length = i;
        enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
        if(VERY_UNLIKELY(stat))
          return stat;

        scanner->cell_start = i + 1;
        scanner->row_start = i + 1;
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if(UNLIKELY(c == '\n')) {
      if((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) == 0) {
        if(scanner_last == '\r') { // ignore; we are outside a cell and last char was rowend
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
        } else {
          // this is a row end
          scanner->scanned_length = i;
          enum zsv_status stat = cell_and_row_dl(scanner, buff + scanner->cell_start, i - scanner->cell_start);
          if(VERY_UNLIKELY(stat))
            return stat;
          scanner->cell_start = i + 1;
          scanner->row_start = i + 1;
        }
        continue; // this char is not part of the cell content
      } else
        // we are inside an open quote, which is needed to escape this char
        scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
    } else if(LIKELY(c == quote)) {
      if(i == scanner->cell_start) {
        scanner->quoted = ZSV_PARSER_QUOTE_UNCLOSED;
        scanner->quote_close_position = 0;
        c = 0;
      } else if(scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED) {
        // the cell started with a quote that is not yet closed
        if(VERY_LIKELY(i + 1 < bytes_read)) {
          if(LIKELY(buff[i+1] != quote)) {
            // buff[i] is the closing quote (not an escaped quote)
            scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
            scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;

            // keep track of closing quote position to handle the edge case
            // where content follows the closing quote e.g. cell content is:
            //  "this-cell"-did-not-need-quotes
            if(LIKELY(scanner->quote_close_position == 0))
              scanner->quote_close_position = i - scanner->cell_start;
          } else {
            // next char is also '"'
            // e.g. cell content is: "this "" is a dbl quote"
            //           cursor is here => ^
            // include in cell content and don't further process
            scanner->quoted |= ZSV_PARSER_QUOTE_NEEDED;
            scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
            skip_next_delim = 1;
          }
        }
      } else {
        // cell_length > 0 and cell did not start w quote, so
        // we have a quote in middle of an unquoted cell
        // process as a normal char
        scanner->quoted |= ZSV_PARSER_QUOTE_EMBEDDED;
        scanner->quote_close_position = 0;
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

#include "zsv_scan_fixed.c"

enum zsv_status zsv_scan(struct zsv_scanner *scanner,
                         unsigned char *buff,
                         size_t bytes_read
                         ) {
  switch(scanner->mode) {
  case ZSV_MODE_FIXED:
    return zsv_scan_fixed(scanner, buff, bytes_read);
  default:
    return zsv_scan_delim(scanner, buff, bytes_read);
  }
}

#define ZSV_BOM "\xef\xbb\xbf"

// optional: set a filter function to filter data before it is processed
// function should return the number of bytes to process. this may be smaller
// but may not be larger than the original number of bytes, and any data modification
// must be done in-place to *buff
enum zsv_status zsv_set_scan_filter(struct zsv_scanner *scanner,
                                               size_t (*filter)(void *ctx,
                                                                unsigned char *buff,
                                                                size_t bytes_read),
                                               void *ctx
                                               ) {
  scanner->filter = filter;
  scanner->filter_ctx = ctx;
  return zsv_status_ok;
}

size_t zsv_filter_write(void *FILEp, unsigned char *buff, size_t bytes_read) {
  fwrite(buff, 1, bytes_read, (FILE *)FILEp);
  return bytes_read;
}

// zsv_parse_string(): *utf8 may not overlap w scanner buffer!
enum zsv_status zsv_parse_string(struct zsv_scanner *scanner,
                                            const unsigned char *utf8,
                                            size_t len) {
  const unsigned char *cursor = utf8;
  while(len) {
    size_t capacity = scanner->buff.size - scanner->partial_row_length;
    size_t bytes_read = len > capacity ? capacity : len;
    memcpy(scanner->buff.buff + scanner->partial_row_length, cursor, len);
    cursor += len;
    len -= bytes_read;
    if(scanner->filter)
      bytes_read = scanner->filter(scanner->filter_ctx,
                                   scanner->buff.buff + scanner->partial_row_length,
                                   bytes_read);
    if(bytes_read)
      return zsv_scan(scanner, scanner->buff.buff, bytes_read);
  }
  return zsv_status_ok;
}

static void apply_callbacks(struct zsv_scanner *scanner) {
  if(scanner->opts.cell) {
    // call the user-provided cell() callback on each cell
    unsigned char saved_quoted = scanner->quoted;
    for(size_t i = 0, j = zsv_column_count(scanner); i < j; i++) {
      struct zsv_cell c = zsv_get_cell(scanner, i);
      scanner->quoted = c.quoted;
      scanner->opts.cell(scanner->opts.ctx, c.str, c.len);
    }
    scanner->quoted = saved_quoted;
  }
  if(scanner->opts.row)
    // call the user-provided row() callback
    scanner->opts.row(scanner->opts.ctx);
}

static void set_callbacks(struct zsv_scanner *scanner);

static char zsv_internal_row_is_blank(zsv_parser parser) {
  for(unsigned int i = 0; i < parser->row.used; i++)
    if(parser->row.cells[i].len)
      return 0;
  return 1;
}

static void skip_to_first_row_w_data(void *ctx) {
  struct zsv_scanner *scanner = ctx;
  if(LIKELY(zsv_internal_row_is_blank(scanner) == 0)) {
    scanner->opts.no_skip_empty_header_rows = 1;
    if(scanner->empty_header_rows) {
      fprintf(stderr, "Warning: skipped %zu empty header rows; suggest using:\n  --skip-head %zu\n",
              scanner->empty_header_rows,
              scanner->empty_header_rows + scanner->opts_orig.rows_to_skip);
    }
    set_callbacks(scanner);
    apply_callbacks(scanner);
  } else // entire row was empty
    scanner->empty_header_rows++;
}

static void skip_header_rows(void *ctx) {
  struct zsv_scanner *scanner = ctx;
  if(scanner->opts.rows_to_skip)
    scanner->opts.rows_to_skip--;
  if(!scanner->opts.rows_to_skip)
    set_callbacks(scanner);
}

static void collate_header_row(void *ctx) {
  struct zsv_scanner *scanner = ctx;
  if(scanner->opts.header_span) {
    --scanner->opts.header_span;

    // save this row
    if(collate_header_append(scanner, &scanner->collate_header))
      scanner->abort = 1;
  }
  if(!scanner->opts.header_span) {
    set_callbacks(scanner);
    if(scanner->opts.row || scanner->opts.cell) {
      if(scanner->collate_header) {
        size_t offset = 0;
        for(size_t i = 0; i < scanner->collate_header->column_count; i++) {
          size_t len_plus1 = scanner->collate_header->lengths[i];
          if(len_plus1) {
            scanner->row.cells[i].len = len_plus1 - 1;
            scanner->row.cells[i].str = scanner->collate_header->buff.buff + offset;
            scanner->row.cells[i].str[len_plus1 - 1] = '\0';
            scanner->row.cells[i].quoted = 1;
          } else {
            scanner->row.cells[i].len = 0;
            scanner->row.cells[i].str = (unsigned char *)"";
          }
          offset += len_plus1;
        }
      }

      apply_callbacks(scanner);
      collate_header_destroy(&scanner->collate_header);
    }
  }
}

static void set_callbacks(struct zsv_scanner *scanner) {
  if(scanner->opts.rows_to_skip) {
    scanner->opts.row = skip_header_rows;
    scanner->opts.cell = NULL;
    scanner->opts.ctx = scanner;
  } else if(scanner->opts.header_span > 1) {
    scanner->opts.row = collate_header_row;
    scanner->opts.cell = NULL;
    scanner->opts.ctx =	scanner;
  } else if(scanner->mode != ZSV_MODE_FIXED && !scanner->opts.no_skip_empty_header_rows) {
    scanner->opts.row = skip_to_first_row_w_data;
    scanner->opts.cell = NULL;
    scanner->opts.ctx = scanner;
  } else {
    scanner->opts.row = scanner->opts_orig.row;
    scanner->opts.cell = scanner->opts_orig.cell;
    scanner->opts.ctx = scanner->opts_orig.ctx;
  }
}

static void zsv_throwaway_row(void *ctx) {
  set_callbacks(ctx);
}

static int zsv_scanner_init(struct zsv_scanner *scanner,
                              struct zsv_opts *opts) {
  size_t need_buff_size = 0; // opts->buffsize
  if(opts->buffsize < opts->max_row_size * 2)
    need_buff_size = opts->max_row_size * 2;
  opts->delimiter = opts->delimiter ? opts->delimiter : ',';
  if(opts->delimiter == '\n' || opts->delimiter == '\r' || opts->delimiter == '"') {
    fprintf(stderr, "warning: ignoring illegal delimiter\n");
    opts->delimiter = ',';
  }

  if(opts->insert_header_row)
    scanner->insert_string = opts->insert_header_row;

  if(opts->buffsize == 0)
    opts->buffsize = ZSV_DEFAULT_SCANNER_BUFFSIZE;
  else if(opts->buffsize < ZSV_MIN_SCANNER_BUFFSIZE)
    opts->buffsize = ZSV_MIN_SCANNER_BUFFSIZE;

  if(opts->buffsize < need_buff_size) {
    opts->max_row_size = opts->buffsize / 2;
    fprintf(stderr, "Warning: max row size set to %u due to buffer size %zu\n", opts->max_row_size, opts->buffsize);
  }

  scanner->in = opts->stream;
  if(!opts->read) {
    scanner->read = (zsv_generic_read)fread;
    if(!opts->stream)
      scanner->in = stdin;
  } else {
    scanner->read = opts->read;
    scanner->in = opts->stream;
  }
  scanner->buff.buff = opts->buff;
  scanner->buff.size = opts->buffsize;

  if(opts->buffsize && !opts->buff) {
    scanner->buff.buff = malloc(opts->buffsize);
    scanner->free_buff = 1;
  }

# ifdef ZSV_EXTRAS
  if(opts->max_rows)
    scanner->progress.max_rows = opts->max_rows;
# endif
  if(scanner->buff.buff) {
    scanner->opts = *opts;
    scanner->opts_orig = *opts;
    if(!scanner->opts.max_columns)
      scanner->opts.max_columns = 1024;
    set_callbacks(scanner);
    if((scanner->row.allocated = scanner->opts.max_columns)
       && (scanner->row.cells = calloc(scanner->row.allocated, sizeof(*scanner->row.cells))))
      return 0;
  }

  return 1;
}
