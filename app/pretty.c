/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <utf8proc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ZSV_COMMAND pretty
#include "zsv_command.h"
#include <zsv/utils/writer.h>
#include <zsv/utils/string.h>

#define ZSV_PRETTY_DEFAULT_LINE_MAX_WIDTH 160
#define ZSV_PRETTY_DEFAULT_COLUMN_MAX_WIDTH 35
#define ZSV_PRETTY_DEFAULT_COLUMN_MIN_WIDTH 2

#define ZSV_PRETTY_DEFAULT_CACHE_MAX 100

struct zsv_pretty_opts {
  size_t line_width_max;
  size_t column_width_min;
  size_t column_width_max;
  size_t cache_max_rows;

  FILE *out;

  unsigned char ignore_header_lengths:1;
  unsigned char markdown:1;
  unsigned char markdown_pad:1;
  unsigned char no_align:1;
  unsigned char dummy:4;
};

enum zsv_pretty_status {
  zsv_pretty_status_ok = 0,
  zsv_pretty_status_error,
  zsv_pretty_status_memory,
  zsv_pretty_status_cache_updated,
  zsv_pretty_status_cache_full,
  // ...
  zsv_pretty_status_end
};

#ifdef WIN32
#include <windows.h>
static size_t get_console_width() {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  int ret;
  ret = GetConsoleScreenBufferInfo(GetStdHandle( STD_OUTPUT_HANDLE ),&csbi);
  if(ret)
    return csbi.dwSize.X;
  return 0;
}
#else
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef HAVE_TGETENT
static size_t get_console_width() { return 0; }

#else
#include <termcap.h>
static size_t get_console_width() {
  char termbuf[2048];
  char *termtype = getenv("TERM");
  if (tgetent(termbuf, termtype) < 0)
    return 0;

  int columns = tgetnum("co");
  return columns > 0 ? columns : 0;
}
#endif // HAVE_TGETENT

#endif // WIN32-else

const unsigned char UTF8_ELLIPSIS[] = { 226,128,166,'\0'}; // e2 80 a6

struct zsv_cached_row {
  struct zsv_cached_row *next;
  unsigned char **values;
  unsigned int count;
};

static struct zsv_cell
zsv_pretty_get_cell(zsv_parser parser,
                           struct zsv_cached_row *r, unsigned ix) {
  struct zsv_cell c;
  memset(&c, 0, sizeof(c));
  if(r) {
    if(ix < r->count)
      c.str = r->values[ix];
    if(!c.str)
      c.str = (unsigned char *)"";
    c.len = strlen((char *)c.str);
  } else
    c = zsv_get_cell(parser, ix);
  return c;
}

static void zsv_cached_rows_delete(struct zsv_cached_row *first) {
  for(struct zsv_cached_row *next, *e = first; e; e = next) {
    next = e->next;
    for(unsigned int i = 0; i < e->count; i++)
      free(e->values[i]);
    free(e->values);
    free(e);
  }
}

static struct zsv_cached_row *zsv_pretty_dupe_row(zsv_parser parser) {
  unsigned col_count = zsv_cell_count(parser);
  if(col_count) {
    struct zsv_cached_row *r = calloc(1, sizeof(*r));
    if(r) {
      r->count = col_count;
      r->values = calloc(r->count, sizeof(*r->values));
      if(!r->values) {
        fprintf(stderr, "Error: out of memory\n");
        free(r);
        return NULL;
      }

      for(unsigned int i = 0; i < r->count; i++) {
        struct zsv_cell cell = zsv_get_cell(parser, i);
        cell.str = (unsigned char *)zsv_strtrim(cell.str, &cell.len);
        if((r->values[i] = malloc(cell.len + 1))) {
          memcpy(r->values[i], cell.str, cell.len);
          r->values[i][cell.len] = '\0';
        }
      }
      return r;
    }
  }
  return NULL;
}

struct zsv_pretty_data {
  int err;
  size_t row_ix;
  zsv_parser parser;
  enum zsv_status parser_status;

  size_t (*write)(const void *, size_t, size_t, void *);
  void *write_arg;

  struct {
    size_t printed;
    size_t max;
  } line;

  struct {
    struct zsv_cached_row *c_rows;
    struct zsv_cached_row **next;
    size_t count;
    size_t max;
    char full;
  } cache;

  struct {
    size_t *values;
    size_t allocated;
    size_t used;
    size_t min, max;
  } widths;

  size_t current_table_lines_written;

  size_t first_column_count;

  unsigned char no_trim:1;
  unsigned char verbose:1;
  unsigned char ignore_header_lengths:1;
  unsigned char markdown:1;
  unsigned char markdown_pad:1;
  unsigned char no_align:1;
  unsigned char dummy:2;
};

static size_t zsv_pretty_get_width(struct zsv_pretty_data *data, size_t ix) {
  if(ix < data->widths.used)
    return data->widths.values[ix];
  return 0;
}

static size_t zsv_pretty_get_row_count(struct zsv_pretty_data *data,
                                         struct zsv_cached_row *r) {
  return r ? r->count : zsv_cell_count(data->parser);
}

static void zsv_pretty_output_lineend(struct zsv_pretty_data *data,
                                        struct zsv_cached_row *r) {
  size_t columns_used = zsv_pretty_get_row_count(data, r);
//  size_t columns_used = zsv_row_cells_count(r);
  char empty_line = columns_used == 0 ||
    (columns_used == 1 &&
     zsv_pretty_get_cell(data->parser, r, 0).len == 0);

  if(empty_line)
    data->current_table_lines_written = 0;
  else if(columns_used > 1) {
    data->current_table_lines_written++;
    if(data->markdown || data->markdown_pad) {
      if(data->current_table_lines_written == 1) {
        data->write("|\n", 1, 2, data->write_arg);
        size_t printed = 0;
        for(unsigned col_ix = 0; printed < data->line.max && col_ix < columns_used; col_ix++) {
          data->write("|", 1, 1, data->write_arg);
          printed++;
          if(data->markdown_pad && col_ix <= data->widths.used) {
            int j = zsv_pretty_get_width(data, col_ix);
            if(!j)
              j = 1;
            for(; j-- && printed <= data->line.max; printed++)
              data->write("-", 1, 1, data->write_arg);
          } else
            data->write("--", 1, 2, data->write_arg);
        }
      }
      data->write("|", 1, 1, data->write_arg);
    }
  }
  data->write("\n", 1, 1, data->write_arg);

  data->line.printed = 0;
}

static void zsv_pretty_output_linestart(struct zsv_pretty_data *data) {
 if(data->markdown || data->markdown_pad)
   data->write("|", 1, 1, data->write_arg);
}


static size_t is_newline(const unsigned char *utf8, int wchar_len) {
  return (wchar_len == 1 && strchr("\n\r", utf8[0] ));
  // add multibyte newline check?
}

// utf8_bytes_up_to_max_width: return number of bytes used up to a maximum screen width
// max will be set to the actual width used
static size_t
utf8_bytes_up_to_max_width_and_replace_newlines(unsigned char *str1, size_t len1,
                                                size_t max_width, size_t *used_width,
                                                int *err) {
  utf8proc_int32_t codepoint1;
  utf8proc_ssize_t bytes_read1;
  size_t width_so_far = *used_width = 0;
  int this_char_width = 0;
  size_t bytes_so_far = 0;
  while(bytes_so_far < len1) {
    bytes_read1 = utf8proc_iterate((utf8proc_uint8_t *)str1 + bytes_so_far, len1, &codepoint1);
    if(!bytes_read1) {
      bytes_read1 = 1;
      *err = 1;
      this_char_width = 1;
    } else if(is_newline(str1 + bytes_so_far, bytes_read1)) {
      memset((void *)(str1 + bytes_so_far), ' ', bytes_read1);
      continue;
    } else {
      this_char_width = utf8proc_charwidth(codepoint1);
      if(width_so_far + this_char_width > max_width) {
        *used_width = width_so_far;
        return bytes_so_far;
      }
    }
    width_so_far += this_char_width;
    bytes_so_far += bytes_read1;
  }
  *used_width = width_so_far;
  return bytes_so_far;
}

void zsv_pretty_write_cell(unsigned char *buff, size_t bytes, struct zsv_pretty_data *data, size_t max_col_width, char last_column) {
  if(data->line.max > data->line.printed) {
    size_t used_width = 0;
    size_t max_width = max_col_width == 0 || max_col_width > data->line.max - data->line.printed ?
      data->line.max - data->line.printed : max_col_width;
    if(bytes) {
      int utf8_err;
      size_t bytes_to_print = utf8_bytes_up_to_max_width_and_replace_newlines(buff, bytes, max_width,
                                                                              &used_width, &utf8_err);
      char ellipsis = 0;
      if(bytes_to_print && bytes_to_print < bytes && max_col_width == max_width && used_width == max_col_width) {
        ellipsis = 1;
        if(max_width > 1)
          bytes_to_print = utf8_bytes_up_to_max_width_and_replace_newlines(buff, bytes, max_width - 1,
                                                                           &used_width, &utf8_err);
      }
      if(bytes_to_print) {
        if((data->markdown || data->markdown_pad)
           && (memchr(buff, '|', bytes_to_print)
               || memchr(buff, '\\', bytes_to_print))
           ) {
          char *tmp = malloc(bytes_to_print*2);
          if(!tmp)
            data->parser_status = zsv_status_memory;
          else {
            size_t tmp_len = 0;
            for(size_t i = 0; i < bytes_to_print; i++) {
              if(memchr("|\\", buff[i], 2))
                tmp[tmp_len++] = '\\';
              tmp[tmp_len++] = buff[i];
            }
            data->write(tmp, 1, tmp_len, data->write_arg);
            free(tmp);
          }
        } else
          data->write(buff, 1, bytes_to_print, data->write_arg);
        data->line.printed += used_width;

        if(ellipsis) {
          data->write((unsigned char *)UTF8_ELLIPSIS, 1, strlen((const char *)UTF8_ELLIPSIS), data->write_arg);
          data->line.printed++;
          used_width++;
        }
      } else
        used_width = 0;
    }

    if(data->markdown_pad
       || (!data->markdown && !last_column)) {
      size_t remaining = max_width - used_width;
      while(remaining) {
        data->write((unsigned char *)" ", 1, 1, data->write_arg);
        data->line.printed++;
        --remaining;
      }
    }
  }
}

#define zsv_pretty_output_col_spacer(data) do { \
  if(data->line.printed < data->line.max) { \
    data->write((unsigned char *)"|", 1, 1, data->write_arg);   \
    data->line.printed++;                                       \
  }                                                             \
  } while(0)

static void zsv_pretty_output_row(struct zsv_pretty_data *data,
                                    struct zsv_cached_row *r) {
  size_t columns_used = zsv_pretty_get_row_count(data, r);
  size_t columns_to_print = data->first_column_count > columns_used ? data->first_column_count : columns_used;
  if(columns_used == 1) {
    struct zsv_cell cell = zsv_pretty_get_cell(data->parser, r, 0);
    if(cell.len)
      zsv_pretty_write_cell(cell.str, cell.len, data, 0, 1);
  } else {
    for(unsigned col_ix = 0; col_ix < columns_used; col_ix++) {
      if(col_ix > 0)
        zsv_pretty_output_col_spacer(data);
      else
        zsv_pretty_output_linestart(data);
      struct zsv_cell cell = zsv_pretty_get_cell(data->parser, r, col_ix);
      zsv_pretty_write_cell(cell.str, cell.len, data, zsv_pretty_get_width(data, col_ix), col_ix + 1 == columns_to_print);
    }
  }

  // pad the output with additional cells, if this row had fewer than the first row
  if(!data->no_align && !data->markdown && !data->markdown_pad) {
    if(data->first_column_count < 2)
      data->first_column_count = columns_used;
    else {
      unsigned char empty[2];
      empty[0] = '\0';
      empty[1] = '\0';
      while(columns_used > 1 && columns_used < data->first_column_count) {
        zsv_pretty_output_col_spacer(data);
        zsv_pretty_write_cell(empty, 0, data,
                                zsv_pretty_get_width(data, columns_used),
                                columns_used + 1 == columns_to_print);
        columns_used++;
      }
    }
  }
  zsv_pretty_output_lineend(data, r);
}

static void zsv_pretty_output_cache(struct zsv_pretty_data *data) {
  for(struct zsv_cached_row *c_row = data->cache.c_rows; c_row; c_row = c_row->next)
    zsv_pretty_output_row(data, c_row);
}

static void zsv_pretty_clear_cache(struct zsv_pretty_data *data) {
  zsv_cached_rows_delete(data->cache.c_rows);
  data->cache.c_rows = NULL;
  data->cache.next = &data->cache.c_rows;
  data->cache.count = 0;
}

static void set_min_col_widths(struct zsv_pretty_data *data, size_t used) {
  for(unsigned i = 0; i < used; i++)
    if(data->widths.values[i] < data->widths.min)
      data->widths.values[i] = data->widths.min;
}

static void zsv_pretty_reset_column_widths(struct zsv_pretty_data *data) {
  zsv_pretty_output_cache(data);
  zsv_pretty_clear_cache(data);
  data->cache.full = 0;
  if(data->widths.used && data->widths.values) {
    memset(data->widths.values, 0, data->widths.used * sizeof(*data->widths.values));
    set_min_col_widths(data, data->widths.used);
  }
  data->widths.used = 0;
  data->first_column_count = 0;
}

static enum zsv_pretty_status zsv_pretty_add_to_cache(struct zsv_pretty_data *data) {
  enum zsv_pretty_status status = zsv_pretty_status_error;
  if(data->cache.full)
    status = zsv_pretty_status_cache_full;
  else {
    struct zsv_cached_row *dupe = zsv_pretty_dupe_row(data->parser);
    if(!dupe)
      status = zsv_pretty_status_memory;
    else {
      *data->cache.next = dupe;
      data->cache.next = &dupe->next;
      if(++data->cache.count >= data->cache.max)
        data->cache.full = 1;
      status = zsv_pretty_status_cache_updated;
    }
  }
  return status;
}

static enum zsv_pretty_status zsv_pretty_update_column_widths(struct zsv_pretty_data *data) {
  size_t columns_used = zsv_cell_count(data->parser);
  if(columns_used > 1) {
    if(columns_used > data->widths.allocated) {
      size_t *new_width_values = realloc(data->widths.values, columns_used * sizeof(*new_width_values));
      if(!new_width_values)
        return zsv_pretty_status_memory;
      // zero out the newly-allocated memory
      for(unsigned int j = data->widths.allocated; j < columns_used; j++)
        new_width_values[j] = data->widths.min;

      data->widths.values = new_width_values;
      data->widths.allocated = columns_used;
    }
    for(unsigned i = 0; i < columns_used; i++) {
      // struct zsv_row_cell v = zsv_row_get_cell(r, i);
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      size_t cell_width;
      int utf8_err;
      utf8_bytes_up_to_max_width_and_replace_newlines(cell.str, cell.len, data->line.max + 1,
                                                      &cell_width, &utf8_err);
      if(cell_width > data->widths.values[i] && data->widths.values[i] < data->widths.max)
        data->widths.values[i] = cell_width > data->widths.max ? data->widths.max : cell_width;
    }
    if(data->widths.used < columns_used)
      data->widths.used = columns_used;
  }
  return zsv_pretty_status_ok;
}

/*
  zsv_pretty_row():
  - if the number of columns == 1, then:
    - output + clear the cache, and output the column
    - alternatively, if data length > 0, add to cache or print if cache is full, but don't update lengths
  - if the number of columns > 1, then:
    - if the cache is not full, update column lengths and cache the column
    - if the cache is full, then:
      - if the cache is not printed, print it
      - output the column with the given lengths
*/
static void zsv_pretty_row(void *ctx) {
  struct zsv_pretty_data *data = ctx;
  if(data->err)
    return;

  unsigned int columns_used = zsv_cell_count(data->parser);
  if(columns_used < 2 && (zsv_pretty_get_cell(data->parser, NULL, 0).len == 0 || data->widths.used == 0)) {
    zsv_pretty_reset_column_widths(data);
    zsv_pretty_output_row(data, NULL);
  } else { // >= 2 columns
    switch(zsv_pretty_add_to_cache(data)) {
    case zsv_pretty_status_cache_updated: // row was added to cache
      if(!data->ignore_header_lengths || data->cache.count > 1)
        zsv_pretty_update_column_widths(data);
      break;
    case zsv_pretty_status_cache_full: // row not added
      if(data->cache.c_rows) {
        zsv_pretty_output_cache(data);
        zsv_pretty_clear_cache(data);
      }
      zsv_pretty_output_row(data, NULL);
      break;
    default:
      break;
    }
  }

  if(++data->row_ix % 1000 == 0 && data->verbose)
    fprintf(stderr, "Processed %zu rows\n", data->row_ix);
}

const char *zsv_pretty_usage_msg[] =
  {
   APPNAME ": print one or more tables of data in fixed-width format",
   "",
   "Usage: " APPNAME " [filename] [--width n]",
   "",
   "Options:",
   "  -o, --output <filename> : save output the the specified file",
   "  -W, --width <n>: set the max line width to output. if not provided",
   "                            will try to detect automatically",
   "  -p, --rows:             : set the number of (preview) rows to calculated widths from",
   "                            if not provided, defaults to 150",
   "  -C, --max-col-width <n> : set the max column width. if not provided, defaults to 50",
   "  -D, --min-col-width <n> : set the min column width. if not provided, defaults to 2",
   "  -A, --no-align          : do not fill in additional cells if the first row has more columns than the current row",
   "  -m, --markdown          : output the table in markdown format",
   "  -M, --markdown-pad      : output the table in markdown format with padding",
   "  -H, --ignore-header-lengths: ignore header lengths in determining column width",
   NULL
  };

static void zsv_pretty_usage() {
  for(int i = 0; zsv_pretty_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_pretty_usage_msg[i]);
}

static void zsv_pretty_flush(struct zsv_pretty_data *data) {
  if(data->parser)
    zsv_finish(data->parser);
  if(data->cache.c_rows) {
    zsv_pretty_output_cache(data);
    zsv_pretty_clear_cache(data);
  }
}

static void zsv_pretty_destroy(struct zsv_pretty_data *data) {
  if(data->parser) {
    zsv_finish(data->parser);
    zsv_delete(data->parser);
  }
//  zsv_row_destroy(data->row);
  zsv_pretty_clear_cache(data);

  if(data->widths.values)
    free(data->widths.values);

  free(data);
}

static struct zsv_pretty_data *zsv_pretty_init(struct zsv_pretty_opts *opts,
                                               struct zsv_opts *parser_opts,
                                               struct zsv_prop_handler *custom_prop_handler,
                                               const char *input_path,
                                               const char *opts_used) {
  struct zsv_pretty_data *data = calloc(1, sizeof(*data));
  if(!data)
    return NULL;

  data->ignore_header_lengths = opts->ignore_header_lengths;
  data->markdown = opts->markdown;
  data->markdown_pad = opts->markdown_pad;
  data->no_align = opts->no_align;

  if(opts->line_width_max)
    data->line.max = opts->line_width_max;
  else if(!(data->line.max = get_console_width()))
    data->line.max = ZSV_PRETTY_DEFAULT_LINE_MAX_WIDTH;

  parser_opts->row_handler = zsv_pretty_row;
  parser_opts->ctx = data;
  zsv_new_with_properties(parser_opts, custom_prop_handler, input_path, opts_used, &data->parser);

  data->write = (size_t (*)(const void *, size_t, size_t, void *))fwrite;
  data->write_arg = opts->out ? opts->out : stdout;

  zsv_pretty_clear_cache(data);

  if(opts->column_width_min)
    data->widths.min = opts->column_width_min;
  else
    data->widths.min = ZSV_PRETTY_DEFAULT_COLUMN_MIN_WIDTH;

  if(opts->column_width_max)
    data->widths.max = opts->column_width_max;
  else
    data->widths.max = ZSV_PRETTY_DEFAULT_COLUMN_MAX_WIDTH;

  if(opts->cache_max_rows)
    data->cache.max = opts->cache_max_rows;
  else
    data->cache.max = ZSV_PRETTY_DEFAULT_CACHE_MAX;

  return data;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *parser_opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsv_pretty_usage();
    return 0;
  }

  int rc = 0;
  FILE *in = stdin;
  const char *input_path = NULL;

  struct zsv_pretty_opts opts = { 0 };

  opts.line_width_max = ZSV_PRETTY_DEFAULT_LINE_MAX_WIDTH;
  opts.column_width_min = ZSV_PRETTY_DEFAULT_COLUMN_MIN_WIDTH;
  opts.column_width_max = ZSV_PRETTY_DEFAULT_COLUMN_MAX_WIDTH;
  opts.cache_max_rows = ZSV_PRETTY_DEFAULT_CACHE_MAX;

  const char *size_t_args[] = {"-W", "--width", "-C", "--max-col-width", "-D", "--min-col-width", "-p", "--rows", NULL };
  size_t size_t_maximums[] = { 32000, 32000, 500, 500, 500, 500, 100000000, 100000000 };
  for(int i = 1; !rc && i < argc; i++) {
    if(*argv[i] == '-') {
      if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
        if(++i >= argc)
          rc = zsv_printerr(1, "%s option requires a filename value", argv[i-1]);
        else if(opts.out)
          rc = zsv_printerr(1, "Output specified more than once");
        else if(!(opts.out = fopen(argv[i], "wb")))
          rc = zsv_printerr(1, "Unable to open for writing: %s", argv[i]);
      } else if(!strcmp(argv[i], "-m") || !strcmp(argv[i], "--markdown"))
        opts.markdown = 1;
      else if(!strcmp(argv[i], "-A") || !strcmp(argv[i], "--no-align"))
        opts.no_align = 1;
      else if(!strcmp(argv[i], "-M") || !strcmp(argv[i], "--markdown-pad"))
        opts.markdown_pad = 1;
      else if(!strcmp(argv[i], "-H") || !strcmp(argv[i], "--ignore-header-lengths"))
        opts.ignore_header_lengths = 1;
      else {
        char got_opt = 0;
        for(int j = 0; size_t_args[j]; j++) {
          if(!strcmp(size_t_args[j], argv[i])) {
            got_opt = 1;
            i++;
            if(i >= argc)
              rc = zsv_printerr(1, "%s option requires a value", size_t_args[j]);
            else if(atol(argv[i]) < 2 || (size_t)atol(argv[i]) > size_t_maximums[j])
              rc = zsv_printerr(1, "%s value must be an integer between 2 and %zu", size_t_args[j], size_t_maximums[j]);
            else {
              switch(j) {
              case 0:
              case 1: // --width
                opts.line_width_max = atol(argv[i]);
                break;
              case 2:
              case 3: // --max-col-width
                opts.column_width_max = atol(argv[i]);
                break;
              case 4:
              case 5: // --min-col-width
                opts.column_width_min = atol(argv[i]);
                break;
              case 6: // --rows
              case 7:
                opts.cache_max_rows = atol(argv[i]);
                break;
              }
            }
            break;
          }
        }
        if(!got_opt)
          rc = zsv_printerr(1, "Unrecognized option: %s", argv[i]);
      }
    } else if(!(in = fopen(argv[i], "rb")))
      rc = zsv_printerr(1, "Unable to open file %s for reading", argv[i]);
    else
      input_path = argv[i];
  }

#ifdef NO_STDIN
  if(in == stdin)
    rc = zsv_printerr(1, "Please specify an input file");
#endif
  if(opts.column_width_min > opts.column_width_max || opts.column_width_min > opts.line_width_max)
    rc = zsv_printerr(1, "Min column width cannot exceed max column width or max line width");

  parser_opts->stream = in;
  struct zsv_pretty_data *h = zsv_pretty_init(&opts, parser_opts, custom_prop_handler, input_path, opts_used);
  if(!h)
    rc = 1;
  else {
    zsv_handle_ctrl_c_signal();
    rc = 0;
    enum zsv_status status;
    while(zsv_parse_more(h->parser) == zsv_status_ok)
      ;

    while(!rc && !zsv_signal_interrupted
          && (status = zsv_parse_more(h->parser)) == zsv_status_ok)
      ;

    zsv_pretty_flush(h);
    zsv_pretty_destroy(h);
  }
  if(opts.out)
    fclose(opts.out);
  if(in && in != stdin)
    fclose(in);

  return rc;
}
