/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood, Matt Wong and Guarnerix dba Liquidaty
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_API_H
#define ZSV_API_H

#define ZSV_ROW_MAX_SIZE_DEFAULT 65536
#define ZSV_ROW_MAX_SIZE_DEFAULT_S "64k"

#define ZSV_MAX_COLS_DEFAULT 1024

#define ZSV_ROW_MAX_SIZE_MIN 1024
#define ZSV_ROW_MAX_SIZE_MIN_S "1024"

#define ZSV_MIN_SCANNER_BUFFSIZE 4096
#define ZSV_DEFAULT_SCANNER_BUFFSIZE (1<<18) // 256k

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define ZSV_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define ZSV_EXPORT
#endif

ZSV_EXPORT
const unsigned char *zsv_parse_status_desc(enum zsv_status status);

ZSV_EXPORT
char zsv_quoted(zsv_parser parser);

ZSV_EXPORT
void zsv_abort(zsv_parser);

/* number of cells in the row */
ZSV_EXPORT
size_t zsv_column_count(zsv_parser parser);

ZSV_EXPORT
char zsv_row_is_blank(zsv_parser parser);

ZSV_EXPORT
struct zsv_cell zsv_get_cell(zsv_parser parser, size_t ix);

ZSV_EXPORT
void zsv_set_row_handler(zsv_parser, void (*row)(void *ctx));

ZSV_EXPORT
void zsv_set_context(zsv_parser parser, void *ctx);

ZSV_EXPORT
void zsv_set_input(zsv_parser, void *in);

ZSV_EXPORT
zsv_parser zsv_new(struct zsv_opts *opts);

ZSV_EXPORT enum zsv_status zsv_parse_more(zsv_parser parser);

ZSV_EXPORT enum zsv_status zsv_next_input(zsv_parser parser, void *f_next_input);

ZSV_EXPORT enum zsv_status zsv_set_malformed_utf8_replace(zsv_parser parser, unsigned char value);
ZSV_EXPORT enum zsv_status zsv_set_scan_filter(zsv_parser parser,
                                               size_t (*filter)(void *ctx,
                                                                unsigned char *buff,
                                                                size_t bytes_read),
                                               void *ctx);

/**
 * Set parsing mode to fixed-width. Once set to fixed mode, a parser may not be
 *   set back to CSV mode
 * @return status code
 * @param parser parser handle
 * @param count number of elements in offsets
 * @param offsets array of cell-end offsets. offsets[0] should be the length of the first cell
 */
ZSV_EXPORT enum zsv_status zsv_set_fixed_offsets(zsv_parser parser, size_t count, size_t *offsets);

ZSV_EXPORT size_t zsv_filter_write(void *FILEp, unsigned char *buff, size_t bytes_read);

// zsv_parse_string(): *utf8 may not overlap w parser buffer!
ZSV_EXPORT enum zsv_status zsv_parse_string(zsv_parser parser,
                                            const unsigned char *restrict utf8,
                                            size_t len);

// return a ptr to remaining (unparsed) buffer and length remaining
ZSV_EXPORT unsigned char *zsv_remaining_buffer(zsv_parser parser, size_t *len);

ZSV_EXPORT void zsv_set_row_handler(zsv_parser handle,
                                        void (*row)(void *ctx));

/**
 * Finish any remaining processing, after all input has been read
 */
ZSV_EXPORT enum zsv_status zsv_finish(zsv_parser);


ZSV_EXPORT enum zsv_status zsv_delete(zsv_parser);

/**
 * @return number of bytes scanned from the last zsv_parse_more() invocation
 */
ZSV_EXPORT size_t zsv_scanned_length(zsv_parser);

/**
 * @return cumulative number of bytes scanned across all requests by this parser
 */
ZSV_EXPORT size_t zsv_cum_scanned_length(zsv_parser parser);

/**
 * Create a zsv_opts structure and return its handle. This is only necessary in
 * environments where structures cannot be directly instantiated such as web
 * assembly
 */
ZSV_EXPORT struct zsv_opts *
zsv_opts_new(
               void (*row)(void *ctx), /* row handler */

               /**
                * usually it is easier to do all processing in the row handler,
                * but you can also / instead use a cell handler
                */
               void (*cell)(void *ctx, unsigned char *utf8_value, size_t len),

               void *ctx,             /* pointer passed to row / cell handler(s) */
               zsv_generic_read read, /* defaults to fread */
               void *stream,          /* defaults to stdin */

               unsigned char *buff,   /* user-provided buff */
               size_t buffsize,       /* size of user-provided buff */

               unsigned max_columns,  /* defaults to 1024 */
               /**
                * max_row_size defaults to 32k
                * the parser will handle rows of *at least* this size (in bytes)
                * buffer is automatically allocated and this is non-zero,
                * buffsize = 2 * max_row_size
                */
               unsigned max_row_size,

               char delimiter, /* defaults to comma */
               char no_quotes  /* defaults to false */

#ifdef ZSV_EXTRAS
               /**
                * maximum number of rows to parse (including any header rows)
                */
               , size_t max_rows
#endif
             );

/**
 * Destroy an option structure that was created by zsv_opts_new()
 */
ZSV_EXPORT void zsv_opts_delete(struct zsv_opts *);

#endif
