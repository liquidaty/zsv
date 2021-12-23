/*
 * Copyright (C) 2021 Tai Chi Minh Ralph Eastwood, Matt Wong and Guarnerix dba Liquidaty
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_COMMON_H
#define ZSV_COMMON_H

#ifdef __cplusplus
# define ZSV_BEGIN_DECL extern "C" {
# define ZSV_END_DECL	}
#else
# define ZSV_BEGIN_DECL
# define ZSV_END_DECL	/* empty */
#endif

enum zsv_status {
  zsv_status_ok = 0,
  zsv_status_cancelled,
  zsv_status_no_more_input
};

typedef struct zsv_scanner * zsv_parser;

/**
 * Structure for returning parsed CSV cell values
 */
struct zsv_cell {
  /**
   * address of cell contents (not null-terminated)
   */
  unsigned char *str;

  /**
   * length of cell contents
   */
  size_t len;

  /**
   * bitfield values for `quoted` flags
   */
#  define ZSV_PARSER_QUOTE_UNCLOSED 1 /* only used internally by parser */
#  define ZSV_PARSER_QUOTE_CLOSED   2 /* value was quoted */
#  define ZSV_PARSER_QUOTE_NEEDED   4 /* value contains delimiter or dbl-quote */
#  define ZSV_PARSER_QUOTE_EMBEDDED 8 /* value contains dbl-quote */
  /**
   * quoted flags enable additional efficiency, in particular when input data will
   * be output as text (csv, json etc), by indicating whether the cell contents may
   * require special handling. For example, if the caller will output the cell value as
   * CSV and quoted == 0, the caller need not scan the cell contents to check if
   * quoting or escaping will be required
   */
  char quoted;
};

typedef size_t (*zsv_generic_write)(const void * restrict,  size_t,  size_t,  void * restrict);
typedef size_t (*zsv_generic_read)(void * restrict, size_t n, size_t size, void * restrict);

# ifdef ZSV_EXTRAS
/**
 * progress callback function signature
 * @param context pointer set in parser opts.progress.ctx
 * @param cumulative_row_count number of input rows read so far
 * @return zero to continue processing, non-zero to cancel parse
 */
typedef int (*zsv_progress_callback)(void *ctx, size_t cumulative_row_count);
# endif

struct zsv_opts {
  // callbacks for handling cell and/or row data
  void (*cell)(void *ctx, unsigned char *utf8_value, size_t len);
  void (*row)(void *ctx);
  void *ctx;
  void (*overflow)(void *ctx, unsigned char *utf8, size_t len);
  void (*error)(void *ctx, enum zsv_status status, const unsigned char *err_msg, size_t err_msg_len, unsigned char bad_c, size_t cum_scanned_length);

  /**
   * caller can specify its own read function for fetching data to be parsed
   * default value is `fread()`
   */
  zsv_generic_read read;

  /**
   * caller can specify its own stream that is passed to the read function
   * default value is stdin
   */
  void *stream;

  /**
   * optionally, the caller can specify its own buffer for the parser to use
   * of at least ZSV_MIN_SCANNER_BUFFSIZE (4096) in size
   */
  unsigned char *buff;

  /**
   * if caller specifies its own buffer, this should be its size
   * otherwise, this is the size of the internal buffer that will be created,
   * subject to increase if/as appropriate if max_row_size is specified.
   * defaults to 256k
   *
   * cli option: -B,--buff-size
   */
  size_t buffsize;

  /**
   * maximum number of columns to parse. defaults to 1024
   *
   * cli option: -c,--max-column-count
   */
  unsigned max_columns;

  /**
   * maximum row size can be used as an alternative way to specify the
   * internal buffer size, which will be at least as large as max_row_size * 2
   *
   * cli option: -r,--max-row-size
   */
  unsigned max_row_size;

  /**
   * delimiter: typically a comma or tab
   * can be any char other than newline, form feed or quote
   * defaults to comma
   *
   * cli option: -t,--tab-delim or -O,--other-delim <delim>
   */
  char delimiter;

  /**
   * no_quotes: if > 0, this flag indicates that the parser should treat double-quotes
   * just like any ordinary character
   * defaults to 0
   *
   * cli option: -q,--no-quote
   */
  char no_quotes;

  /**
   * flag to print more verbose messages to the console
   * cli option: -v,--verbose
   */
  char verbose;

  /**
   * if the actual data does not have a header row with column names, the caller
   * should provide one (in CSV format) which will be treated as if it was the
   * first row of data
   */
  const char *insert_header_row;

#  ifdef ZSV_EXTRAS
  struct {
    size_t frequency; // number of rows between progress callback calls
    zsv_progress_callback callback;
    void *ctx;
  } progress;
# endif
};

#  ifdef ZSV_EXTRAS
/**
 * set or get default parser options
 */
void zsv_set_default_opts(struct zsv_opts);

struct zsv_opts zsv_get_default_opts();

/**
 * set the default option progress callback (e.g. from wasm where `struct zsv_opts`
 * cannot be independently accessed set
 * @param cb callback to call
 * @param ctx pointer passed to callback
 * @param frequency number of rows to parse between progress calls
 */
void zsv_set_default_progress_callback(zsv_progress_callback cb, void *ctx, size_t frequency);
#  else
#   define zsv_get_default_opts() { 0 }
#  endif

#endif
