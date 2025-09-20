/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sglib.h>

#define ZSV_COMMAND stack
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/string.h>

const char *zsv_stack_usage_msg[] = {
  APPNAME ": stack one or more csv files vertically, aligning columns with the same name",
  "",
  "Usage: " APPNAME " [options] filename [filename...]",
  "",
  "Options:",
  "  -o <filename>      : output file",
  "  -b                 : output with BOM",
  "  -q                 : always add double-quotes",
  //  "  --prepend-filename : output source filename as the first column of each output row",
  "  --unique           : only output unique column names;",
  "                       in case of duplicates, only the last (right-most) column will be kept",
  NULL,
};

static int zsv_stack_usage(void) {
  for (size_t i = 0; zsv_stack_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_stack_usage_msg[i]);
  return 0;
}

typedef struct zsv_stack_colname {
  struct zsv_stack_colname *next;
  char unsigned *orig_name;
  char unsigned *name;
  unsigned occurrence;
  unsigned dupes;
  size_t global_position;
  unsigned char color;
  struct zsv_stack_colname *left;
  struct zsv_stack_colname *right;
  unsigned int raw_col_ix_plus_1;
} zsv_stack_colname;

static struct zsv_stack_colname *zsv_stack_colname_init(struct zsv_stack_colname *e, const unsigned char *name,
                                                        size_t len, unsigned occurrence) {
  memset(e, 0, sizeof(*e));
  if (len)
    e->name = zsv_strtolowercase(name, &len);
  else
    e->name = calloc(1, 2);
  e->occurrence = occurrence;
  return e;
}

static void zsv_stack_colname_free(struct zsv_stack_colname *e) {
  if (e->name)
    free(e->name);
  if (e->orig_name)
    free(e->orig_name);
}

static void zsv_stack_colname_delete(struct zsv_stack_colname *e) {
  zsv_stack_colname_free(e);
  free(e);
}

static int zsv_stack_colname_cmp(zsv_stack_colname *x, zsv_stack_colname *y) {
  int i = strcmp((const char *)x->name, (const char *)y->name);
  if (i == 0)
    return x->occurrence == y->occurrence ? 0 : x->occurrence > y->occurrence ? 1 : -1;
  return i;
}

SGLIB_DEFINE_RBTREE_PROTOTYPES(zsv_stack_colname, left, right, color, zsv_stack_colname_cmp);
SGLIB_DEFINE_RBTREE_FUNCTIONS(zsv_stack_colname, left, right, color, zsv_stack_colname_cmp);

static void zsv_stack_colname_tree_delete(zsv_stack_colname **tree) {
  if (tree && *tree) {
    struct sglib_zsv_stack_colname_iterator it;
    struct zsv_stack_colname *e;
    for (e = sglib_zsv_stack_colname_it_init(&it, *tree); e; e = sglib_zsv_stack_colname_it_next(&it))
      zsv_stack_colname_delete(e);
    *tree = NULL;
  }
}

struct zsv_stack_data;
struct zsv_stack_input_file {
  struct zsv_stack_input_file *next;
  FILE *f;
  const char *fname;
  zsv_parser parser;

  // output_column_map[x] = n where x = output col ix, n = 0 if no map, else raw_column_ix
  zsv_stack_colname *colnames;
  size_t *output_column_map;
  size_t output_column_map_size;
  size_t header_row_end_offset; // location in buff at which the data row begins
  struct zsv_stack_data *ctx;
  unsigned char headers_done : 1;
  unsigned char _ : 7;
};

struct zsv_stack_data {
  struct zsv_stack_input_file *inputs;
  struct zsv_stack_input_file *current_input;

  unsigned inputs_count;
  unsigned current_input_ix;
  zsv_stack_colname *colnames;
  zsv_stack_colname *first_colname;
  zsv_stack_colname *last_colname;
  unsigned colnames_count;
  int err;

  zsv_csv_writer csv_writer;

  unsigned char unique_column_names : 1;
  unsigned char _ : 7;
};

static struct zsv_stack_input_file **zsv_stack_input_file_add(const char *filename,
                                                              struct zsv_stack_input_file **target, unsigned *count,
                                                              FILE *f, struct zsv_stack_data *ctx) {
  struct zsv_stack_input_file *e = calloc(1, sizeof(*e));
  if (e) {
    e->f = f;
    e->fname = filename;
    e->ctx = ctx;
    *target = e;
    (*count)++;
    return &e->next;
  }
  return target;
}

static void zsv_stack_input_files_delete(struct zsv_stack_input_file *list) {
  for (struct zsv_stack_input_file *next, *e = list; e; e = next) {
    next = e->next;
    if (e->f)
      fclose(e->f);
    if (e->output_column_map)
      free(e->output_column_map);
    zsv_stack_colname_tree_delete(&e->colnames);
    zsv_delete(e->parser);
    free(e);
  }
}

static void zsv_stack_cleanup(struct zsv_stack_data *data) {
  zsv_stack_input_files_delete(data->inputs);
  zsv_stack_colname_tree_delete(&data->colnames);
  zsv_writer_delete(data->csv_writer);
}

static struct zsv_stack_colname *zsv_stack_colname_get_or_add(struct zsv_cell c, unsigned occurrence,
                                                              struct zsv_stack_colname **tree, int *added) {
  const unsigned char *name = c.str;
  size_t name_len = c.len;
  struct zsv_stack_colname e;
  zsv_stack_colname_init(&e, name, name_len, occurrence);
  struct zsv_stack_colname *found = sglib_zsv_stack_colname_find_member(*tree, &e);
  if (found) {
    found->dupes++;
    zsv_stack_colname_free(&e);
  } else {
    found = calloc(1, sizeof(*found));
    if (found) {
      *added = 1;
      *found = e;
      if (name)
        found->orig_name = name ? zsv_memdup(name, name_len) : calloc(1, 2);
      sglib_zsv_stack_colname_add(tree, found);
    }
  }
  return found;
}

// zsv_stack_consolidate_header(): return global position
static unsigned zsv_stack_consolidate_header(struct zsv_stack_data *d, struct zsv_cell cell, unsigned occurrence) {
  int added = 0;
  zsv_stack_colname *c = zsv_stack_colname_get_or_add(cell, occurrence, &d->colnames, &added);
  if (!c)
    d->err = 1;
  else {
    if (added) {
      c->global_position = ++d->colnames_count;
      if (d->last_colname)
        d->last_colname->next = c;
      else
        d->first_colname = c;
      d->last_colname = c;
    }
    return c->global_position;
  }
  return 0;
}

static void zsv_stack_header_row(void *ctx) {
  struct zsv_stack_input_file *input = ctx;
  if (!input->headers_done && !zsv_row_is_blank(input->parser)) { // skip any blank leading rows
    input->headers_done = 1;
    zsv_abort(input->parser);
  }
}

static void zsv_stack_data_row(void *ctx) {
  struct zsv_stack_input_file *input = ctx;
  if (!input->headers_done) {
    input->headers_done = 1;
    return;
  }
  if (!zsv_row_is_blank(input->parser)) {
    size_t colnames_count = input->ctx->colnames_count;
    for (unsigned i = 0; i < colnames_count; i++) {
      size_t raw_ix_plus_1;
      if (i < input->output_column_map_size && ((raw_ix_plus_1 = input->output_column_map[i]))) {
        struct zsv_cell cell = zsv_get_cell(input->parser, raw_ix_plus_1 - 1);
        zsv_writer_cell(input->ctx->csv_writer, !i, cell.str, cell.len, cell.quoted);
      } else
        zsv_writer_cell(input->ctx->csv_writer, !i, 0x0, 0, 0);
    }
  }
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts,
                               struct zsv_prop_handler *custom_prop_handler) {
  int err = 0;
  if (argc < 2) {
    zsv_stack_usage();
    return 1;
  }

  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
    return zsv_stack_usage();

  struct zsv_opts saved_opts = *opts;
  struct zsv_stack_data data = {0};
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
  writer_opts.stream = stdout;

  struct zsv_stack_input_file **next_input = &data.inputs;
  for (int arg_i = 1; !data.err && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
    if (!strcmp(arg, "-b"))
      writer_opts.with_bom = 1;
    //    else if (!strcmp(arg, "--prepend-filename")_)
    //      data.prepend_filename
    else if (!strcmp(arg, "--unique"))
      data.unique_column_names = 1;
    else if (!strcmp(arg, "-o")) {
      arg_i++;
      if (arg_i >= argc)
        fprintf(stderr, "-o option: no filename specified\n");
      else {
        if (!(writer_opts.stream = fopen(argv[arg_i], "wb"))) {
          data.err = 1;
          fprintf(stderr, "Unable to open file for writing: %s\n", argv[arg_i]);
        }
      }
    } else if (*arg == '-') {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      data.err = 1;
    } else {
      FILE *f = fopen(arg, "rb");
      if (!f) {
        fprintf(stderr, "Could not open file for reading: %s\n", arg);
        data.err = 1;
      } else
        next_input = zsv_stack_input_file_add(arg, next_input, &data.inputs_count, f, &data);
    }
  }

  if (!(data.csv_writer = zsv_writer_new(&writer_opts)))
    data.err = 1;

  // collect all header names so we can line them up
  unsigned i = 0;
  for (struct zsv_stack_input_file *input = data.inputs; !data.err && input; input = input->next, i++) {
    *opts = saved_opts;
    opts->row_handler = zsv_stack_header_row;
    opts->ctx = input;

    // to do: max_cell_size
    opts->stream = input->f;
    if (zsv_new_with_properties(opts, custom_prop_handler, input->fname, &input->parser) != zsv_status_ok)
      data.err = 1;
    else {
      zsv_handle_ctrl_c_signal();
      enum zsv_status status;
      while (!data.err && !input->headers_done && (status = zsv_parse_more(input->parser)) == zsv_status_ok)
        ;
    }
  }

  // we have read the header row of each input, so:
  // - consolidate a column header names into a single list
  // - assign output->input column mappings for each input

  // first, get maximum size of consolidated cols, which is just the sum of the column count
  // of all inputs
  size_t max_columns_count = 0;
  for (struct zsv_stack_input_file *input = data.inputs; !data.err && input; input = input->next)
    max_columns_count += zsv_cell_count(input->parser);

  // next, for each input, align the input columns with the output columns
  for (struct zsv_stack_input_file *input = data.inputs; input && !data.err; input = input->next) {
    if (max_columns_count) {
      if (!(input->output_column_map = calloc(max_columns_count, sizeof(*input->output_column_map))))
        data.err = 1;
      else {
        input->output_column_map_size = max_columns_count;
        // assign column indexes to global columns
        size_t cols_used = zsv_cell_count(input->parser);
        for (unsigned col_ix = 0; col_ix < cols_used; col_ix++) {
          struct zsv_cell cell = zsv_get_cell(input->parser, col_ix);
          unsigned occurrence = 0;
          if (!data.unique_column_names) {
            int added;
            zsv_stack_colname *c = zsv_stack_colname_get_or_add(cell, 0, &input->colnames, &added);
            if (c)
              occurrence = c->dupes;
          }
          size_t output_ix = zsv_stack_consolidate_header(&data, cell, occurrence);
          if (output_ix)
            input->output_column_map[output_ix - 1] = col_ix + 1;
        }
      }
    }
    zsv_delete(input->parser);
    input->parser = NULL;
  }

  // not necessary, but free up unused memory by resizing each input's output_column_map
  for (struct zsv_stack_input_file *input = data.inputs; input && !data.err; input = input->next) {
    if (!data.colnames_count) {
      if (input->output_column_map) {
        free(input->output_column_map);
        input->output_column_map = NULL;
      }
    } else {
      size_t *resized = realloc(input->output_column_map, data.colnames_count * sizeof(*resized));
      if (resized)
        input->output_column_map = resized;
    }
  }

  // print headers
  for (struct zsv_stack_colname *e = data.first_colname; !data.err && e; e = e->next) {
    const unsigned char *name = e->orig_name ? e->orig_name : e->name ? e->name : (const unsigned char *)"";
    zsv_writer_cell(data.csv_writer, e == data.first_colname, name, strlen((const char *)name), 1);
  }

  // process data
  for (struct zsv_stack_input_file *input = data.inputs; input && !data.err; input = input->next, i++) {
    if (input->headers_done) {
      *opts = saved_opts;
      opts->row_handler = zsv_stack_data_row;
      opts->ctx = input;

      rewind(input->f);
      input->headers_done = 0;
      opts->stream = input->f;
      if (zsv_new_with_properties(opts, custom_prop_handler, input->fname, &input->parser) != zsv_status_ok)
        data.err = 1;
      else {
        enum zsv_status status = zsv_status_ok;
        while (status == zsv_status_ok && !data.err && (status = zsv_parse_more(input->parser)) == zsv_status_ok)
          ;
        zsv_finish(input->parser);
        zsv_delete(input->parser);
        input->parser = NULL;
      }
    }
  }
  err = data.err;
  zsv_stack_cleanup(&data);

  if (writer_opts.stream && writer_opts.stream != stdout)
    fclose(writer_opts.stream);

  return err;
}
