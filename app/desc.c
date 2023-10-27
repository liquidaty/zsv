/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <sglib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <time.h>
#include <unistd.h> // unlink()

#define ZSV_COMMAND desc
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/string.h>

#define ZSV_DESC_MAX_COLS_DEFAULT 32768
#define ZSV_DESC_MAX_COLS_DEFAULT_S "32768"

#define ZSV_DESC_FLAG_MINMAX 1
#define ZSV_DESC_FLAG_MINMAXLEN 2
#define ZSV_DESC_FLAG_UNIQUE 32
#define ZSV_DESC_FLAG_UNIQUE_CI 64

struct zsv_desc_string_list {
  struct zsv_desc_string_list *next;
  unsigned char *value;
  size_t count;
};

void zsv_desc_string_list_free(struct zsv_desc_string_list *e) {
  struct zsv_desc_string_list *n;
  for( ; e; e = n) {
    n = e->next;
    if(e->value)
      free(e->value);
    free(e);
  }
}

typedef struct zsv_desc_unique_key {
  unsigned char color:1;
  unsigned char _:7;
  unsigned char *value;
  struct zsv_desc_unique_key *left;
  struct zsv_desc_unique_key *right;
} zsv_desc_unique_key;

struct zsv_desc_unique_key_container {
  struct zsv_desc_unique_key *key;
  size_t max_count;
  size_t count;
  unsigned char not_enum:1;
  unsigned char dummy:7;
};

static struct zsv_desc_unique_key *zsv_desc_unique_key_new(const unsigned char *value, size_t len) {
  zsv_desc_unique_key *key = calloc(1, sizeof(*key));
  if(!key || !(key->value = malloc(len + 1)))
    ; // handle out-of-memory error!
  else {
    memcpy(key->value, value, len);
    key->value[len] = '\0';
  }
  return key;
}

static void zsv_desc_unique_key_delete(zsv_desc_unique_key *e) {
  if(e)
    free(e->value);
  free(e);
}

static int zsv_desc_unique_key_cmp(zsv_desc_unique_key *x, zsv_desc_unique_key *y) {
  return strcmp((const char *)x->value, (const char *)y->value);
}

SGLIB_DEFINE_RBTREE_PROTOTYPES(zsv_desc_unique_key, left, right, color, zsv_desc_unique_key_cmp);
SGLIB_DEFINE_RBTREE_FUNCTIONS(zsv_desc_unique_key, left, right, color, zsv_desc_unique_key_cmp);

#define ZSV_DESC_MAX_EXAMPLE_COUNT 5 // could make this customizable...
struct zsv_desc_column_data {
  char *name;
  unsigned int position;

  unsigned char not_unique:1;
  unsigned char not_unique_ci:1;
  unsigned char _:6;

  struct zsv_desc_unique_key_container unique_values;
  struct zsv_desc_unique_key_container unique_values_ci;
  struct zsv_desc_string_list *examples;
  struct zsv_desc_string_list **examples_tail;
  unsigned int examples_count;

  unsigned int total_count;
  struct {
    unsigned int count;
  } mblank;

  struct {
    size_t lo;
    size_t hi;
  } lengths;
};

static void zsv_desc_column_data_finalize(struct zsv_desc_column_data *col, unsigned int i) {
  col->position = i;
}

static void zsv_desc_column_unique_values_delete(zsv_desc_unique_key **tree) {
  if(tree && *tree) {
    struct sglib_zsv_desc_unique_key_iterator it;
    struct zsv_desc_unique_key *e;
    for(e=sglib_zsv_desc_unique_key_it_init(&it,*tree); e; e=sglib_zsv_desc_unique_key_it_next(&it))
      zsv_desc_unique_key_delete(e);
    *tree = NULL;
  }
}

static void zsv_desc_column_data_free(struct zsv_desc_column_data *e) {
  free(e->name);
  zsv_desc_column_unique_values_delete(&e->unique_values.key);
  zsv_desc_column_unique_values_delete(&e->unique_values_ci.key);
  zsv_desc_string_list_free(e->examples);
}

struct zsv_desc_column_name {
  struct zsv_desc_column_name *next;
  char *name;
};


static void zsv_desc_column_names_delete(struct zsv_desc_column_name **p) {
  if(p && *p) {
    struct zsv_desc_column_name *next;
    for(struct zsv_desc_column_name *e = *p; e; e = next) {
      next = e->next;
      free(e->name);
      free(e);
    }
    *p = NULL;
  }
}

enum zsv_desc_status {
  zsv_desc_status_ok = 0,
  zsv_desc_status_error, // generic error
  zsv_desc_status_memory,
  zsv_desc_status_file,
  zsv_desc_status_argument
};

struct zsv_desc_data {
  struct zsv_opts *opts;
  const char *input_filename;
  zsv_csv_writer csv_writer;

  char header_only;
  char *filename;

  void (*header_func)(void *ctx, unsigned int col_ix, const char *name); // api use only
  void *header_func_arg;
  unsigned int errcount;

  unsigned int max_cols;
  unsigned int current_column_ix;

  struct zsv_desc_column_name *column_names;
  struct zsv_desc_column_name **column_names_tail;

  unsigned int col_count;
  struct zsv_desc_column_data *columns;

#define ZSV_DESC_MAX_ENUM_DEFAULT 100
  size_t max_enum;
  size_t row_count;

  char *err_msg;
  enum zsv_desc_status err;

  zsv_parser parser;

  unsigned char flags; // see ZSV_DESC_FLAG_XXX
  unsigned char done;

  size_t max_row_size;

  char *overflowed;
  size_t overflow_count;

  unsigned char quick:1;
  unsigned char _:7;
};

static void zsv_desc_finalize(struct zsv_desc_data *data) {
  for(unsigned int i = 0; i < data->col_count; i++)
    zsv_desc_column_data_finalize(&data->columns[i], i);
}

static void write_headers(struct zsv_desc_data *data) {
  // to do: adjust header for ZSV_DESC_FLAG options
  const char *headers1[] =
    {
     "#",
     "Column name",
     "Min Length",
     "Max Length",
     NULL
    };
    const char *headers2[] = {
     "Count",
     "Blank %",
     "Example 1",
     "Example 2",
     "Example 3",
     "Example 4",
     "Example 5",
     NULL
    };
  for(int i = 0; headers1[i]; i++)
    zsv_writer_cell(data->csv_writer, i == 0,
                      (const unsigned char *)headers1[i],
                      strlen(headers1[i]), 1);

  if(data->flags & ZSV_DESC_FLAG_UNIQUE)
    zsv_writer_cell_s(data->csv_writer, 0, (const unsigned char *)"Unique", 0);

  if(data->flags & ZSV_DESC_FLAG_UNIQUE_CI)
    zsv_writer_cell_s(data->csv_writer, 0, (const unsigned char *)"Unique (case-insensitive)", 0);

  for(int i = 0; headers2[i]; i++)
    zsv_writer_cell(data->csv_writer, 0,
                      (const unsigned char *)headers2[i],
                      strlen(headers2[i]), 1);
}

static void zsv_desc_print(struct zsv_desc_data *data) {
  if(data->header_only) {
    for(unsigned int i = 0; i < data->col_count; i++) {
      struct zsv_desc_column_data *c = &data->columns[i];
      zsv_writer_cell(data->csv_writer, 1, (const unsigned char *)c->name,
                      c->name ? strlen(c->name) : 0, 1);
    }
  } else {
    write_headers(data);
    for(unsigned int i = 0; i < data->col_count; i++) {
      struct zsv_desc_column_data *c = &data->columns[i];
      zsv_writer_cell_zu(data->csv_writer, 1, i+1);
      zsv_writer_cell_s(data->csv_writer, 0, (unsigned char *)c->name, 1);
      if(c->lengths.lo) {
        zsv_writer_cell_zu(data->csv_writer, 0, c->lengths.lo);
        zsv_writer_cell_zu(data->csv_writer, 0, c->lengths.hi);
      } else {
        zsv_writer_cell(data->csv_writer, 0, NULL, 0, 0);
        zsv_writer_cell(data->csv_writer, 0, NULL, 0, 0);
      }

      // unique
      if(data->flags & ZSV_DESC_FLAG_UNIQUE) {
        const char *s = c->not_unique ? "FALSE" : "TRUE";
        zsv_writer_cell_s(data->csv_writer, 0, (const unsigned char *)s, 0);
      }

      // unique_ci
      if(data->flags & ZSV_DESC_FLAG_UNIQUE_CI) {
        const char *s = c->not_unique_ci ? "FALSE" : "TRUE";
        zsv_writer_cell_s(data->csv_writer, 0, (const unsigned char *)s, 0);
      }

      // count, blank %
      zsv_writer_cell_zu(data->csv_writer, 0, c->total_count);
      zsv_writer_cell_Lf(data->csv_writer, 0, ".2",
                           ((long double)c->mblank.count) / (long double) (c->total_count)
                           * (long double)100);

      for(struct zsv_desc_string_list *sl = c->examples; sl; sl = sl->next) {
        if(sl->count) {
          char *tmp;
          asprintf(&tmp, "%s (%zu)", sl->value, sl->count + 1);
          zsv_writer_cell_s(data->csv_writer, 0, (unsigned char *)tmp, 1);
          free(tmp);
        } else
          zsv_writer_cell_s(data->csv_writer, 0, sl->value, 1);
      }
    }
  }
}

static void zsv_desc_set_err(struct zsv_desc_data *data, enum zsv_desc_status err, char *msg) {
  data->err = err;
  if(msg) {
    if(data->err_msg)
      free(msg);
    else
      data->err_msg = msg;
  }
}

// zsv_desc_column_update_unique(): return 1 if unique, 0 if dupe
static int zsv_desc_column_update_unique(struct zsv_desc_unique_key_container *key_container,
                                     const unsigned char *utf8_value, size_t len) {
  zsv_desc_unique_key *key = zsv_desc_unique_key_new(utf8_value, len);
  if(sglib_zsv_desc_unique_key_find_member(key_container->key, key)) { // not unique
    if(key_container->count > key_container->max_count) {
      zsv_desc_column_unique_values_delete(&key_container->key);
      key_container->not_enum = 1;
    }
    zsv_desc_unique_key_delete(key);
    return 0;
  } else {
    sglib_zsv_desc_unique_key_add(&key_container->key, key);
    key_container->count++;
    return 1;
  }
}

static void zsv_desc_cell(void *ctx, unsigned char *restrict utf8_value, size_t len) {
  struct zsv_desc_data *data = ctx;
  if(!data || data->err || data->done)
    return;

  // trim the cell values, so we don't count e.g. " abc" as different from "abc"
  utf8_value = (unsigned char *)zsv_strtrim(utf8_value, &len);
  if(data->row_count == 0) {
    if(data->current_column_ix < data->max_cols) {
      struct zsv_desc_column_name *e = calloc(1, sizeof(*e));
      if(!e) {
        zsv_desc_set_err(data, zsv_desc_status_memory, NULL);
        return;
      }

      if(len)
        e->name = zsv_memdup(utf8_value, len);
      *data->column_names_tail = e;
      data->column_names_tail = &e->next;
    }
  } else {
    if(data->current_column_ix < data->col_count) {
      struct zsv_desc_column_data *col = &data->columns[data->current_column_ix];
      if(col) {
        col->total_count++;
        if(!len)
          col->mblank.count++;
        else {
          if(col->lengths.lo == 0 || len < col->lengths.lo)
            col->lengths.lo = len;
          if(len > col->lengths.hi)
            col->lengths.hi = len;
          if(col->examples_count < ZSV_DESC_MAX_EXAMPLE_COUNT || !data->quick) {
            char already_have = 0;
            if(!col->examples_tail)
              col->examples_tail = &col->examples;
            for(struct zsv_desc_string_list *sl = col->examples; !already_have && sl; sl = sl->next) {
              if(sl->value
                 && !zsv_strincmp(utf8_value, len, sl->value, strlen((char *)sl->value))) {
                already_have = 1;
                sl->count++;
              }
            }
            if(!already_have && col->examples_count < ZSV_DESC_MAX_EXAMPLE_COUNT) {
              struct zsv_desc_string_list *sl;
              if((sl = *col->examples_tail = calloc(1, sizeof(*sl)))) {
                col->examples_tail = &sl->next;
                sl->value = zsv_memdup(utf8_value, len);
                col->examples_count++;
              }
            }
          }

          if(data->flags & ZSV_DESC_FLAG_UNIQUE) {
            if(!col->not_unique)
              if(!zsv_desc_column_update_unique(&col->unique_values, utf8_value, len)) // dupe
                col->not_unique = 1;
          }

          if(data->flags & ZSV_DESC_FLAG_UNIQUE_CI) {
            if(!col->not_unique_ci ||
               !col->unique_values_ci.not_enum
               // )
               ) {
              unsigned char *lc = zsv_strtolowercase(utf8_value, &len);
              if(lc) {
                if(!zsv_desc_column_update_unique(&col->unique_values_ci, lc, len))
                  col->not_unique_ci = 1;
                free(lc);
              }
            }
          }
        }
      }
    }
  }
  data->current_column_ix++;
}

static void zsv_desc_row(void *ctx) {
  struct zsv_desc_data *data = ctx;

  if(!data || data->err)
    return;

  if(data->row_count == 0) {
    if(data->current_column_ix < data->max_cols)
      data->col_count = data->current_column_ix;
    else
      data->current_column_ix = data->max_cols;
    if(!(data->columns = calloc(data->col_count, sizeof(*data->columns)))) {
      zsv_desc_set_err(data, zsv_desc_status_memory, NULL);
      return;
    }

    struct zsv_desc_column_name *cn = data->column_names;
    for(unsigned int i = 0; i < data->col_count && cn; cn = cn->next, i++) {
      struct zsv_desc_column_data *col = &data->columns[i];
      if(cn->name && *cn->name) {
        col->name = cn->name;
        cn->name = NULL;
      }
      col->unique_values_ci.max_count = data->max_enum;
    }

    if(data->header_only)
      data->done = 1;
  } else {
    if(data->row_count % 50000 == 0 && data->opts->verbose)
      fprintf(stderr, "%zu rows read\n", data->row_count);
  }

  data->current_column_ix = 0;
  ++data->row_count;
}

const char *zsv_desc_usage_msg[] =
  {
   APPNAME ": get column-level information about a table's content",
   "",
   "Usage: " APPNAME " [<options>] <filename, or - for stdin>",
   "  Describes a CSV table",
   "  e.g. " APPNAME " data.csv",
   "       " APPNAME " data.csv -a",
   "",
   "Options:",
   "  -b, --with-bom : output with BOM",
   "  -C <maximum_number_of_columns>: defaults to 1024",
   "  -H: only output header names",
   "  -q, --quick: minimize example counts,",
   "  -a, --all: calculate all metadata (for now, this only adds uniqueness info)",
   "  -o <output filename>: name of file to save output to (defaults to stdout)",
   NULL
  };

static int zsv_desc_usage() {
  for(int i = 0; zsv_desc_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_desc_usage_msg[i]);
  return 0;
}

static void zsv_desc_cleanup(struct zsv_desc_data *data) {
  if(data->columns) {
    for(unsigned int i = 0; i < data->col_count; i++)
      zsv_desc_column_data_free(&data->columns[i]);
    free(data->columns);
    data->columns = NULL;
  }

  zsv_desc_column_names_delete(&data->column_names);
  free(data->err_msg);
  data->err_msg = NULL;

  if(data->opts->stream && data->opts->stream != stdin) {
    fclose(data->opts->stream);
    data->opts->stream = NULL;
  }

  if(data->overflowed) {
    fprintf(stderr, "Warning: data overflowed %zu times (example: %s)\n", data->overflow_count, data->overflowed);
    free(data->overflowed);
  }
  zsv_writer_delete(data->csv_writer);
}

#define ZSV_DESC_TMPFN_TEMPLATE "zsv_desc_XXXXXXXXXXXX"

static void zsv_desc_execute(struct zsv_desc_data *data,
                             struct zsv_prop_handler *custom_prop_handler,
                             const char *input_path,
                             const char *opts_used) {
  data->opts->cell_handler = zsv_desc_cell;
  data->opts->row_handler = zsv_desc_row;
  data->opts->ctx = data;

  if(!data->max_enum)
    data->max_enum = ZSV_DESC_MAX_ENUM_DEFAULT;
  if(zsv_new_with_properties(data->opts, custom_prop_handler, input_path, opts_used, &data->parser)
     == zsv_status_ok) {
    FILE *input_temp_file = NULL;
    enum zsv_status status;
    if(input_temp_file)
      zsv_set_scan_filter(data->parser, zsv_filter_write, input_temp_file);
    while(!zsv_signal_interrupted && (status = zsv_parse_more(data->parser)) == zsv_status_ok)
      ;

    if(input_temp_file)
      fclose(input_temp_file);
    zsv_finish(data->parser);
    zsv_delete(data->parser);
  }
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if(argc < 1)
    zsv_desc_usage();
  else if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    zsv_desc_usage();
  else {
    struct zsv_desc_data data = { 0 };
    const char *input_path = NULL;
    int err = 0;
    if(opts->malformed_utf8_replace != ZSV_MALFORMED_UTF8_DO_NOT_REPLACE) // user specified to be 'none'
      opts->malformed_utf8_replace = '?';

    data.opts = opts;
    data.max_cols = ZSV_DESC_MAX_COLS_DEFAULT; // default
    data.column_names_tail = &data.column_names;

    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();

    for(int arg_i = 1; !data.err && arg_i < argc; arg_i++) {
      if(!strcmp(argv[arg_i], "-b") || !strcmp(argv[arg_i], "--with-bom"))
        writer_opts.with_bom = 1;
      else if(!strcmp(argv[arg_i], "-o") || !strcmp(argv[arg_i], "--output")) {
        if(++arg_i >= argc)
          data.err = zsv_printerr(zsv_desc_status_error,
                                  "%s option requires a filename", argv[arg_i-1]);
        else if(!(writer_opts.stream = fopen(argv[arg_i], "wb")))
          data.err = zsv_printerr(zsv_desc_status_error,
                                  "Unable to open for write: %s", argv[arg_i]);
      } else if(!strcmp(argv[arg_i], "-a") || !strcmp(argv[arg_i], "--all"))
        data.flags = 0xff;
      else if(!strcmp(argv[arg_i], "-q") || !strcmp(argv[arg_i], "--quick"))
        data.quick = 1;
      else if(!strcmp(argv[arg_i], "-H"))
        data.header_only = 1;
      else if(!strcmp(argv[arg_i], "-C")) {
        arg_i++;
        if(!(arg_i < argc && atoi(argv[arg_i]) > 9))
          data.err = zsv_printerr(zsv_desc_status_error, "-C (max cols) invalid: should be positive integer > 9 (got %s)", argv[arg_i]);
        else
          data.max_cols = atoi(argv[arg_i]);
      }
      else {
        if(data.opts->stream) {
          err = 1;
          fprintf(stderr, "Input file specified twice, or unrecognized argument: %s\n", argv[arg_i]);
        } else if(!(data.opts->stream = fopen(argv[arg_i], "rb"))) {
          err = 1;
          fprintf(stderr, "Could not open for reading: %s\n", argv[arg_i]);
        } else
          input_path = argv[arg_i];

        if(data.opts->stream && data.opts->stream != stdin)
          data.input_filename = argv[arg_i];
        if(err)
          data.err = err;
      }
    }

    zsv_handle_ctrl_c_signal();

    if(!data.err && !(data.csv_writer = zsv_writer_new(&writer_opts)))
      data.err = zsv_printerr(zsv_desc_status_error, "Unable to create csv writer");

    if(!data.opts->stream) {
#ifdef NO_STDIN
      data.err = zsv_printerr(zsv_desc_status_error, "Please specify an input file");
#else
      data.opts->stream = stdin;
#endif
    }

    if(data.err) {
      zsv_desc_cleanup(&data);
      return 1;
    }

    zsv_desc_execute(&data, custom_prop_handler, input_path, opts_used);
    zsv_desc_finalize(&data);
    zsv_desc_print(&data);
    zsv_desc_cleanup(&data);
  }
  return 0;
}
