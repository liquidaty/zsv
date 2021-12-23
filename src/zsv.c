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

#include "zsv_internal.c"

ZSV_EXPORT
enum zsv_status zsv_parse_more(struct zsv_scanner *scanner) {
  if(scanner->insert_string) {
    size_t len = strlen(scanner->insert_string);
    if(len > scanner->buff.size)
      len = scanner->buff.size - 1;
    memcpy(scanner->buff.buff + scanner->partial_row_length, scanner->insert_string, len);
    if(scanner->buff.buff[len] != '\n')
      scanner->buff.buff[len] = '\n';
    zsv_scan(scanner, scanner->buff.buff, len + 1);
    scanner->insert_string = NULL;
  }
  // if this is not the first parse call, we might have a partial
  // row at the end of our buffer that must be moved
  if(scanner->old_bytes_read) {
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

    scanner->last = scanner->buff.buff[scanner->old_bytes_read-1];
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
    if(VERY_UNLIKELY(row1(scanner)))
      return zsv_status_cancelled;

    // throw away the next row end
    scanner->row_orig = scanner->opts.row;
    scanner->row_ctx_orig = scanner->opts.ctx;

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
  if(LIKELY(bytes_read))
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
  for(unsigned int i = 0; i < parser->row.used; i++)
    if(parser->row.cells[i].len)
      return 0;
  return 1;
}

// to do: rename to zsv_column_count(). rename all other zsv_hand to just zsv_
ZSV_EXPORT
size_t zsv_column_count(zsv_parser parser) {
  return parser->row.used;
}

ZSV_EXPORT
void zsv_set_row_handler(zsv_parser parser, void (*row)(void *ctx)) {
  parser->opts.row = row;
}

ZSV_EXPORT
void zsv_set_context(zsv_parser parser, void *ctx) {
  parser->opts.ctx = ctx;
}

ZSV_EXPORT
char zsv_quoted(zsv_parser parser) {
  return parser->quoted;
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

// to do: simplify. do not require buff or buffsize
ZSV_EXPORT
zsv_parser zsv_new(struct zsv_opts *opts) {
  struct zsv_opts tmp;
  if(!opts) {
    opts = &tmp;
    memset(opts, 0, sizeof(*opts));
  }
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
enum zsv_status zsv_delete(zsv_parser parser) {
  if(parser) {
    if(parser->free_buff && parser->buff.buff)
      free(parser->buff.buff);

    if(parser->row.cells)
      free(parser->row.cells);
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

#ifdef ZSV_EXTRAS

static struct zsv_opts zsv_default_opts = { 0 };

ZSV_EXPORT
struct zsv_opts zsv_get_default_opts() {
  return zsv_default_opts;
}

ZSV_EXPORT void zsv_set_default_opts(struct zsv_opts opts) {
  zsv_default_opts = opts;
}

ZSV_EXPORT
void zsv_set_default_progress_callback(zsv_progress_callback cb, void *ctx, size_t rows_interval, unsigned int seconds_interval) {
  zsv_default_opts.progress.callback = cb;
  zsv_default_opts.progress.ctx = ctx;
  zsv_default_opts.progress.rows_interval = rows_interval;
  zsv_default_opts.progress.seconds_interval = seconds_interval;
}
#endif
