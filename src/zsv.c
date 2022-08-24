/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood (self), Matt Wong (Guarnerix Inc dba Liquidaty)
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_MIN_SCANNER_BUFFSIZE
#define ZSV_MIN_SCANNER_BUFFSIZE 4096
#endif

#include "zsv.h"
#include <zsv/utils/utf8.h>
#include <zsv/utils/compiler.h>
#ifdef ZSV_EXTRAS
#include <zsv/utils/arg.h>
#endif

#include "zsv_internal.c"

ZSV_EXPORT
const char *zsv_lib_version() {
  return VERSION;
}

ZSV_EXPORT
enum zsv_status zsv_parse_more(struct zsv_scanner *scanner) {
  if(scanner->insert_string) {
    size_t len = strlen(scanner->insert_string);
    if(len > scanner->buff.size)
      len = scanner->buff.size - 1; // to do: throw an error instead
    memcpy(scanner->buff.buff + scanner->partial_row_length, scanner->insert_string, len);
    if(scanner->buff.buff[len] != '\n')
      scanner->buff.buff[len] = '\n';
    zsv_scan(scanner, scanner->buff.buff, len + 1);
    scanner->insert_string = NULL;
  }
  // if this is not the first parse call, we might have a partial
  // row at the end of our buffer that must be moved
  scanner->last = '\0';
  if(scanner->old_bytes_read) {
    scanner->last = scanner->buff.buff[scanner->old_bytes_read-1];
    if(scanner->row_start < scanner->old_bytes_read) {
      size_t len = scanner->old_bytes_read - scanner->row_start;

      if(len < scanner->row_start)
        memcpy(scanner->buff.buff, scanner->buff.buff + scanner->row_start, len);
      else
        memmove(scanner->buff.buff, scanner->buff.buff + scanner->row_start, len);
      scanner->partial_row_length = len;
    } else {
      scanner->cell_start = 0;
      scanner->row_start = 0;
      zsv_clear_cell(scanner);
    }

    scanner->cell_start -= scanner->row_start;
    for(size_t i2 = 0; i2 < scanner->row.used; i2++)
      scanner->row.cells[i2].str -= scanner->row_start;
    scanner->row_start = 0;
    scanner->old_bytes_read = 0;
  }

  scanner->cum_scanned_length += scanner->scanned_length;
  size_t capacity = scanner->buff.size - scanner->partial_row_length;

  if(VERY_UNLIKELY(capacity == 0)) { // our row size was too small to fit a single row of data
    fprintf(stderr, "Warning: row truncated\n");
    if(scanner->mode == ZSV_MODE_FIXED) {
      if(VERY_UNLIKELY(row_fx(scanner, scanner->buff.buff, 0, scanner->buff.size)))
        return zsv_status_cancelled;
    } else if(VERY_UNLIKELY(row_dl(scanner)))
      return zsv_status_cancelled;

    // throw away the next row end
    scanner->opts.row = zsv_throwaway_row;
    scanner->opts.ctx = scanner;

    scanner->partial_row_length = 0;
    capacity = scanner->buff.size;
  }

  size_t bytes_read;

  if(UNLIKELY(!scanner->checked_bom)) {

#ifdef ZSV_EXTRAS
    // initialize progress timer
    if(scanner->opts.progress.seconds_interval)
      scanner->progress.last_time = time(NULL);
#endif

    size_t bom_len = strlen(ZSV_BOM);
    scanner->checked_bom = 1;
    if(scanner->read(scanner->buff.buff, 1, bom_len, scanner->in) == bom_len
       && !memcmp(scanner->buff.buff, ZSV_BOM, bom_len)) {
      // have bom. disregard what we just read
      bytes_read = scanner->read(scanner->buff.buff, 1, capacity, scanner->in);
      scanner->had_bom = 1;
    } else // no BOM. keep the bytes we just read
      bytes_read = bom_len + scanner->read(scanner->buff.buff + bom_len, 1, capacity - bom_len, scanner->in);
  } else // already checked bom. read as usual
    bytes_read = scanner->read(scanner->buff.buff + scanner->partial_row_length, 1,
                               capacity, scanner->in);
  if(UNLIKELY(scanner->filter != NULL))
    bytes_read = scanner->filter(scanner->filter_ctx,
                                 scanner->buff.buff + scanner->partial_row_length, bytes_read);
  if(VERY_LIKELY(bytes_read))
    return zsv_scan(scanner, scanner->buff.buff, bytes_read);
  scanner->scanned_length = scanner->partial_row_length;
  return zsv_status_no_more_input;
}

ZSV_EXPORT
void zsv_abort(zsv_parser parser) {
  parser->abort = 1;
}

ZSV_EXPORT
char zsv_row_is_blank(zsv_parser parser) {
  return zsv_internal_row_is_blank(parser);
}

// to do: rename to zsv_column_count(). rename all other zsv_hand to just zsv_
ZSV_EXPORT
size_t zsv_column_count(zsv_parser parser) {
  return parser->row.used;
}

ZSV_EXPORT
void zsv_set_row_handler(zsv_parser parser, void (*row)(void *ctx)) {
  if(parser->opts.row == parser->opts_orig.row)
    parser->opts.row = row;
  parser->opts_orig.row = row;
}

ZSV_EXPORT
void zsv_set_context(zsv_parser parser, void *ctx) {
  if(parser->opts.ctx == parser->opts_orig.ctx)
    parser->opts.ctx = ctx;
  parser->opts_orig.ctx = ctx;
}

ZSV_EXPORT
char zsv_quoted(zsv_parser parser) {
  return parser->quoted || parser->opts.no_quotes;
}

ZSV_EXPORT
struct zsv_cell zsv_get_cell(zsv_parser parser, size_t ix) {
  if(ix < parser->row.used)
    return parser->row.cells[ix];

  struct zsv_cell c = { 0, 0, 0 };
  return c;
}

ZSV_EXPORT
void zsv_set_input(zsv_parser parser, void *in) {
  parser->in = in;
}

ZSV_EXPORT enum zsv_status zsv_set_fixed_offsets(zsv_parser parser, size_t count, size_t *offsets) {
  if(!count) {
    fprintf(stderr, "Fixed offset count must be greater than zero\n");
    return zsv_status_invalid_option;
  }
  if(offsets[0] == 0)
    fprintf(stderr, "Warning: first cell width is zero\n");
  for(size_t i = 1; i < count; i++) {
    if(offsets[i-1] > offsets[i]) {
      fprintf(stderr, "Invalid offset %zu may not exceed prior offset %zu\n",
              offsets[i], offsets[i-1]);
      return zsv_status_invalid_option;
    } else if(offsets[i-1] == offsets[i])
      fprintf(stderr, "Warning: offset %zu repeated, will always yield empty cell\n",
              offsets[i-1]);
  }

  if(offsets[count-1] > parser->buff.size) {
    fprintf(stderr, "Offset %zu exceeds total buffer size %zu\n", offsets[count-1], parser->buff.size);
    return zsv_status_invalid_option;
  }
  if(parser->cum_scanned_length) {
    fprintf(stderr, "Scanner mode cannot be changed after parsing has begun\n");
    return zsv_status_invalid_option;
  }

  free(parser->fixed.offsets);
  parser->fixed.offsets = calloc(count, sizeof(*parser->fixed.offsets));
  if(!parser->fixed.offsets) {
    fprintf(stderr, "Out of memory!\n");
    return zsv_status_memory;
  }
  parser->fixed.count = count;
  for(unsigned i = 0; i < count; i++)
    parser->fixed.offsets[i] = offsets[i];

  parser->mode = ZSV_MODE_FIXED;
  parser->checked_bom = 1;

  set_callbacks(parser);

  return zsv_status_ok;
}

// to do: simplify. do not require buff or buffsize
ZSV_EXPORT
zsv_parser zsv_new(struct zsv_opts *opts) {
  struct zsv_opts tmp;
  if(!opts) {
    opts = &tmp;
    memset(opts, 0, sizeof(*opts));
  }
  if(!opts->max_row_size)
    opts->max_row_size = ZSV_ROW_MAX_SIZE_DEFAULT;
  if(!opts->max_columns)
    opts->max_columns = ZSV_MAX_COLS_DEFAULT;
  if(opts->delimiter == '\n' || opts->delimiter == '\r' || opts->delimiter == '"') {
    fprintf(stderr, "Invalid delimiter\n");
    return NULL;
  }
  struct zsv_scanner *scanner = calloc(1, sizeof(*scanner));
  if(scanner) {
    if(zsv_scanner_init(scanner, opts)) {
      zsv_delete(scanner);
      scanner = NULL;
    }
  }
  return scanner;
}

ZSV_EXPORT
enum zsv_status zsv_finish(struct zsv_scanner *scanner) {
  enum zsv_status stat = zsv_status_ok;
  if(!scanner->abort) {
    if(scanner->mode == ZSV_MODE_FIXED) {
      if(scanner->partial_row_length)
        return row_fx(scanner, scanner->buff.buff, 0, scanner->partial_row_length);
      return zsv_status_ok;
    }

    if((scanner->quoted & ZSV_PARSER_QUOTE_UNCLOSED)
       && scanner->partial_row_length > scanner->cell_start + 1) {
      int quote = '"';
      scanner->quoted |= ZSV_PARSER_QUOTE_CLOSED;
      scanner->quoted -= ZSV_PARSER_QUOTE_UNCLOSED;
      if(scanner->last == quote)
        scanner->quote_close_position = scanner->partial_row_length - scanner->cell_start;
      else {
        scanner->quote_close_position = scanner->partial_row_length - scanner->cell_start + 1;
        scanner->scanned_length++;
      }
    }
  }

  if(!scanner->finished) {
    scanner->finished = 1;
    if(!scanner->abort) {
      if(scanner->scanned_length > scanner->cell_start)
        cell_dl(scanner, scanner->buff.buff + scanner->cell_start,
                scanner->scanned_length - scanner->cell_start, 1);
      if(scanner->have_cell)
        if(row_dl(scanner))
          stat = zsv_status_cancelled;
    } else
      stat = zsv_status_cancelled;
#ifdef ZSV_EXTRAS
    if(scanner->opts.completed.callback)
      scanner->opts.completed.callback(scanner->opts.completed.ctx, stat);
#endif
  }
  return stat;
}

ZSV_EXPORT
enum zsv_status zsv_delete(zsv_parser parser) {
  if(parser) {
    if(parser->free_buff && parser->buff.buff)
      free(parser->buff.buff);

    free(parser->row.cells);
    free(parser->fixed.offsets);
    collate_header_destroy(&parser->collate_header);
    free(parser);
  }
  return zsv_status_ok;
}

ZSV_EXPORT
const unsigned char *zsv_parse_status_desc(enum zsv_status status) {
  switch(status) {
  case zsv_status_ok:
    return (unsigned char *) "OK";
  case zsv_status_cancelled:
    return (unsigned char *)"User cancelled";
  case zsv_status_no_more_input:
    return (unsigned char *)"No more input";
  case zsv_status_invalid_option:
    return (unsigned char *)"Invalid option";
  case zsv_status_memory:
    return (unsigned char *)"Out of memory";
#ifdef ZSV_EXTRAS
  case zsv_status_max_rows_read:
    return (unsigned char *)"Maximum specified rows have been parsed";
#endif
  }
  return (unsigned char *)"Unknown";
}

ZSV_EXPORT
size_t zsv_scanned_length(zsv_parser parser) {
  return parser->scanned_length;
}

ZSV_EXPORT
unsigned char *zsv_remaining_buffer(struct zsv_scanner *scanner,
                                      size_t *len) {
  if(scanner->scanned_length < scanner->buffer_end) {
    *len = scanner->buffer_end - scanner->scanned_length;
    return scanner->buff.buff + scanner->scanned_length;
  }
  *len = 0;
  return NULL;
}

ZSV_EXPORT
size_t zsv_cum_scanned_length(zsv_parser parser) {
  return parser->cum_scanned_length + parser->scanned_length + (parser->had_bom ? strlen(ZSV_BOM) : 0);
}
