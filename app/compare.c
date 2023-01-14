/*
 * Copyright (C) 2023 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jsonwriter.h>

#include <zsv/utils/string.h>
#include <zsv/utils/writer.h>

#define ZSV_COMMAND compare
#include "zsv_command.h"

#include "compare.h"
#include "compare_internal.h"

/*
static void zsv_compare_output_scalar(struct zsv_compare_data *data,
                                      struct zsv_cell *value) {
//  if(data->writer.type == 'j')
//    jsonwriter_strn(data->jsw, value->str, value->len);
//  else
}
*/

static void zsv_compare_output_tuple(struct zsv_compare_data *data,
                                     struct zsv_compare_input *id_input,
                                     zsv_compare_unique_colname *output_col,
                                     struct zsv_cell *values) {
  // print ID | Column | Value 1 | ... | Value N
  if(data->writer.type == 'j') {
    jsonwriter_start_array(data->writer.handle.jsw);
  // to do: output ID values and column name
  } else {
    // to do: output ID values
    if(!data->id_names) // id is effectively just row number
      zsv_writer_cell_zu(data->writer.handle.csv, ZSV_WRITER_NEW_ROW, data->row_count);
    else {
      for(unsigned idx = 0; idx < id_input->id_count; idx++) {
        struct zsv_cell *c = &id_input->ids[idx].value;
        zsv_writer_cell(data->writer.handle.csv,
                        idx == 0 ? ZSV_WRITER_NEW_ROW : ZSV_WRITER_SAME_ROW,
                        c->str, c->len, c->quoted);
      }
    }

    // output column name of this cell
    zsv_writer_cell_s(data->writer.handle.csv, ZSV_WRITER_SAME_ROW, output_col->name, 1);
  }

  for(unsigned i = 0; i < data->input_count; i++) {
    struct zsv_compare_input *input = &data->inputs[i];
    if(input->done || !input->row_loaded) { // no data for this input
      if(data->writer.type == 'j')
        jsonwriter_null(data->writer.handle.jsw);
      else
        zsv_writer_cell_s(data->writer.handle.csv, ZSV_WRITER_SAME_ROW, (const unsigned char *)"", 0);
    } else {
      struct zsv_cell *value = &values[i];
      if(data->writer.type == 'j')
        jsonwriter_strn(data->writer.handle.jsw, value->str, value->len);
      else
        zsv_writer_cell(data->writer.handle.csv, ZSV_WRITER_SAME_ROW,
                        value->str, value->len, value->quoted);
    }
  }

  if(data->writer.type == 'j')
    jsonwriter_end_array(data->writer.handle.jsw);
}

static void zsv_compare_print_row(struct zsv_compare_data *data,
                                  const unsigned first_ix,
                                  const unsigned last_ix) {
  // for now, output format is simple: for each value,
  // output a single scalar if the values are the same,
  // and a tuple if they differ

  struct zsv_cell *values = calloc(data->input_count, sizeof(*values));
  if(!values) {
    data->status = zsv_compare_status_memory;
    return;
  }

  for(unsigned output_ix = 0; output_ix < data->output_colcount; output_ix++) {
    char different = 0;
    zsv_compare_unique_colname *output_col = NULL;
    struct zsv_compare_input *id_input = NULL;

    for(unsigned i = first_ix; i <= last_ix; i++) {
      struct zsv_compare_input *input = &data->inputs[i];
      if(input->done || !input->row_loaded) continue;

      unsigned col_ix_plus_1 = input->out2in[output_ix];
      if(col_ix_plus_1 == 0)
        values[i].len = 0;
      else {
        unsigned input_col_ix = col_ix_plus_1 - 1;
        if(!output_col)
          output_col = input->output_colnames[input_col_ix];
        if(!id_input)
          id_input = input;
        values[i] = zsv_get_cell(input->parser, input_col_ix);
        if(i != first_ix && !different && data->cmp(data->cmp_ctx, values[first_ix], values[i]))
          different = 1;
      }
    }
//    if(!different) // output scalar
//      zsv_compare_output_scalar(data, &values[first_ix]);
//    else // output tuple
    if(different)
      zsv_compare_output_tuple(data, id_input, output_col, values);
  }
  free(values);
}

static void zsv_compare_input_free(struct zsv_compare_input *input) {
  zsv_delete(input->parser);
  zsv_compare_unique_colnames_delete(&input->colnames);
  free(input->out2in);
  if(input->stream)
    fclose(input->stream);
  free(input->output_colnames);
}

static struct zsv_compare_unique_colname *
zsv_compare_unique_colname_new(const unsigned char *name, size_t len,
                               unsigned instance_num) {
  zsv_compare_unique_colname *col = calloc(1, sizeof(*col));
  if(!col || !(col->name = malloc(len + 1)))
    ; // handle out-of-memory error!
  else {
    memcpy(col->name, name, len);
    col->name[len] = '\0';
    col->name_len = len;
    col->instance_num = instance_num;
  }
  return col;
}

/*
static void unmatched_row_handler(void *ctx, struct zsv_compare_input *id_input,
                                  struct zsv_compare_input *unmatched_row_input) {
  fprintf(stderr, "yo unmatched row\n");
}
*/

static int zsv_compare_unique_colname_cmp(zsv_compare_unique_colname *x, zsv_compare_unique_colname *y) {
  return x->instance_num == y->instance_num ?
    zsv_stricmp(x->name, y->name) : x->instance_num > y->instance_num ? 1 : 0;
}

SGLIB_DEFINE_RBTREE_FUNCTIONS(zsv_compare_unique_colname, left, right, color, zsv_compare_unique_colname_cmp);

static void zsv_compare_unique_colname_delete(zsv_compare_unique_colname *e) {
  if(e)
    free(e->name);
  free(e);
}

static void zsv_compare_unique_colnames_delete(zsv_compare_unique_colname **tree) {
  if(tree && *tree) {
    struct sglib_zsv_compare_unique_colname_iterator it;
    struct zsv_compare_unique_colname *e;
    for(e=sglib_zsv_compare_unique_colname_it_init(&it,*tree); e; e=sglib_zsv_compare_unique_colname_it_next(&it))
      zsv_compare_unique_colname_delete(e);
    *tree = NULL;
  }
}

// zsv_desc_column_update_unique(): return 1 if unique, 0 if dupe
static zsv_compare_unique_colname *
zsv_compare_unique_colname_add_if_not_found(struct zsv_compare_unique_colname **tree,
                                            const unsigned char *utf8_value, size_t len,
                                            unsigned instance_num,
                                            int *added) {
  *added = 0;
  zsv_compare_unique_colname *col = zsv_compare_unique_colname_new(utf8_value, len, instance_num);
  zsv_compare_unique_colname *found = sglib_zsv_compare_unique_colname_find_member(*tree, col);
  if(found) // not unique
    zsv_compare_unique_colname_delete(col);
  else {
    sglib_zsv_compare_unique_colname_add(tree, col);
    *added = 1;
    found = col;
  }
  return found;
}

enum zsv_compare_status zsv_compare_set_inputs(struct zsv_compare_data *data, unsigned count) {
  if(!(data->inputs = calloc(count, sizeof(*data->inputs))))
    return zsv_compare_status_memory;
  data->input_count = count;
  return zsv_compare_status_ok;
}

static int zsv_compare_cell(void *ctx, struct zsv_cell c1, struct zsv_cell c2);

static void zsv_compare_start(struct zsv_compare_data *data) {
  if(data->writer.type == 'j') {
    if(!(data->writer.handle.jsw = jsonwriter_new(stdout))) // to do: data->out
      data->status = zsv_compare_status_memory;
    else
      jsonwriter_start_array(data->writer.handle.jsw);
  } else {
    if(!(data->writer.handle.csv = zsv_writer_new(NULL)))
      data->status = zsv_compare_status_memory;
    else {
      // write header row

      // write ids
      if(!data->id_names) // id is effectively just row number
        zsv_writer_cell_s(data->writer.handle.csv, ZSV_WRITER_NEW_ROW, (const unsigned char *)"Row #", 0);
      else {
        for(struct zsv_compare_id_name *id_name = data->id_names; id_name; id_name = id_name->next) {
          zsv_writer_cell_s(data->writer.handle.csv,
                            id_name == data->id_names ? ZSV_WRITER_NEW_ROW : ZSV_WRITER_SAME_ROW,
                            (const unsigned char *)id_name->name, 1);
        }
      }

      // write "Column"
      zsv_writer_cell_s(data->writer.handle.csv, ZSV_WRITER_SAME_ROW, (const unsigned char *)"Column", 0);

      // write input name(s)
      for(unsigned i = 0; i < data->input_count; i++) {
        struct zsv_compare_input *input = &data->inputs[i];
        zsv_writer_cell_s(data->writer.handle.csv, ZSV_WRITER_SAME_ROW, (const unsigned char *)input->path, 1);
      }
    }
  }
}

static void zsv_compare_end(struct zsv_compare_data *data) {
  if(data->writer.type == 'j') {
    jsonwriter_flush(data->writer.handle.jsw);
    jsonwriter_end(data->writer.handle.jsw);
  } else {
    zsv_writer_flush(data->writer.handle.csv);
  }
}

zsv_compare_handle zsv_compare_new() {
  zsv_compare_handle z = calloc(1, sizeof(*z));
  zsv_compare_set_comparison(z, zsv_compare_cell, z);
//  zsv_compare_set_unmatched_row_handler(unmatched_row_handler, z);
  z->output_colnames_next = &z->output_colnames;
  return z;
}


static void zsv_compare_data_free(struct zsv_compare_data *data) {
  if(data->writer.type == 'j')
    jsonwriter_delete(data->writer.handle.jsw);
  else
    zsv_writer_delete(data->writer.handle.csv);

  for(unsigned i = 0; i < data->input_count; i++)
    zsv_compare_input_free(&data->inputs[i]);
  free(data->inputs);

  zsv_compare_unique_colnames_delete(&data->output_colnames);
}

void zsv_compare_delete(zsv_compare_handle z) {
  if(z) {
    zsv_compare_data_free(z);
    free(z);
  }
}

void zsv_compare_set_comparison(struct zsv_compare_data *data,
                                zsv_compare_cell_func cmp,
                                void *cmp_ctx) {
  data->cmp = cmp;
  data->cmp_ctx = cmp_ctx;
}

/*
void zsv_compare_set_unmatched_row_handler(struct zsv_compare_data *data,
                                           void (*unmatched_row_handler)(void *ctx,
                                                                         struct zsv_compare_input *id_input,
                                                                         struct zsv_compare_input *unmatched_row_input),
                                           void *unmatched_row_handler_ctx) {
  data->unmatched_row_handler = unmatched_row_handler;
  data->unmatched_row_handler_ctx = unmatched_row_handler_ctx;
}
*/

static int zsv_compare_cell(void *ctx, struct zsv_cell c1, struct zsv_cell c2) {
  (void)(ctx);
  return zsv_strincmp(c1.str, c1.len,
                      c2.str, c2.len);
}

static enum zsv_compare_status zsv_compare_advance(struct zsv_compare_data *data) {
  // advance each input (if not row_loaded) to their next row
  char got = 0;
  for(unsigned i = 0; i < data->input_count; i++) {
    struct zsv_compare_input *input = &data->inputs[i];
    if(input->done) continue;
    if(input->row_loaded) continue;
    if(zsv_next_row(input->parser) != zsv_status_row)
      input->done = 1;
    else {
      for(unsigned idx = 0; idx < input->id_count; idx++)
        input->ids[idx].value = zsv_get_cell(input->parser, input->ids[idx].col_ix);
      input->row_loaded = 1;
      got = 1;
    }
  }
  return got ? zsv_compare_status_ok : zsv_compare_status_no_more_input;
}

static int zsv_compare_input_cmp(const void *inputx, const void *inputy) {
  const struct zsv_compare_input *x = inputx;
  const struct zsv_compare_input *y = inputy;

  if(!x->row_loaded && !y->row_loaded) return 0;

  int cmp = 0;
  for(unsigned i = 0; !cmp && i < x->id_count && i < y->id_count; i++)
    // for multibyte input, the input must be also sorted lexicographically
    // to avoid potential mismatches
    // see e.g. https://stackoverflow.com/questions/4611302/sorting-utf-8-strings
    cmp = zsv_strincmp(x->ids[i].value.str, x->ids[i].value.len,
                       y->ids[i].value.str, y->ids[i].value.len);
  return cmp;
}


static enum zsv_compare_status zsv_compare_next(struct zsv_compare_data *data) {
  data->status = zsv_compare_advance(data);
  if(data->status != zsv_compare_status_ok) return data->status;

  data->row_count++;
  // sort the inputs by ID value first, and input position second
  // for as many inputs have the same smallest ID values, output them as a group
  //   and set input->row_loaded to 0
  qsort(data->inputs, data->input_count,
        sizeof(data->inputs[0]), zsv_compare_input_cmp);

  // find the next subset of inputs with identical id values and process it
  for(unsigned i = 0; i < data->input_count; i++) {
    unsigned first = i;
    unsigned last = i;
    for(unsigned tmp_i = i + 1; tmp_i < data->input_count; tmp_i++) {
      struct zsv_compare_input *tmp = &data->inputs[tmp_i];
      if(!tmp->row_loaded) continue;
      if(!zsv_compare_input_cmp(&data->inputs[first], tmp)) { // ids are the same
        last = tmp_i;
        continue;
      }

      // ids are different
      break;
    }

    // print row
    zsv_compare_print_row(data, first, last);

    // reset row_loaded
    for(unsigned tmp = first; tmp <= last; tmp++)
      data->inputs[tmp].row_loaded = 0;

    // update counter
    i = last;
  }
  return zsv_compare_status_ok;
}

static int compare_usage() {
  static const char *usage[] = {
    "Usage: compare [options] [file1.csv] [file2.csv] [...]",
    "Options:",
    " -h,--help             : show usage",
    " --allow-dupes         : allow duplicate column names",
//    " -i,--input <filename> : use specified file input\n"
//    " -I,--no-case          : use case-insensitive comparison\n"
//    " --format-table        :
    NULL
  };

  for(unsigned i = 0; usage[i]; i++)
    printf("%s\n", usage[i]);
  printf("\n");
  return 0;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, const char *opts_used) {
  if(argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    compare_usage();
    return argc < 2 ? 1 : 0;
  }

  const char **input_filenames = calloc(argc, sizeof(*input_filenames)); // temporarily hold the input file names
  if(!input_filenames)
    return zsv_compare_status_memory;

  zsv_compare_handle data = zsv_compare_new();
  if(!data) {
    free(input_filenames);
    return zsv_compare_status_memory;
  }

  // initialization starts here. to do: make this a separate function
  unsigned input_count = 0;
  for(int arg_i = 1; data->status == zsv_compare_status_ok && arg_i < argc;
      arg_i++) {
    const char *arg = argv[arg_i];
#include <zsv/utils/arg.h>
    if(!strcmp(arg, "--allow-duplicate-column-names"))
      data->allow_duplicate_column_names = 1;
    else
      input_filenames[input_count++] = arg;
  }

  if(!input_count)
    data->status = zsv_compare_status_error;
  else {
    if((data->status = zsv_compare_set_inputs(data, input_count)) == zsv_compare_status_ok) {
      // initialize parsers
      for(unsigned ix = 0; data->status == zsv_compare_status_ok && ix < input_count; ix++) {
        struct zsv_compare_input *input = &data->inputs[ix];
        input->path = input_filenames[ix];
        if(!(input->stream = fopen(input->path, "rb"))) {
          perror(input->path);
          data->status = zsv_compare_status_error;
          break;
        }
        struct zsv_opts these_opts = *opts;
        these_opts.stream = input->stream;
        enum zsv_status stat = zsv_new_with_properties(&these_opts, input->path, opts_used, &input->parser);
        if(stat != zsv_status_ok)
          data->status = zsv_compare_status_error;
      }
    }

    char started = 0;
    if(data->status == zsv_compare_status_ok) {
      started = 1;
      zsv_compare_start(data);
      // load header rows, determine total number of output columns amongst all
      for(unsigned i = 0; data->status == zsv_compare_status_ok && i < data->input_count; i++) {
        struct zsv_compare_input *input = &data->inputs[i];
        if(zsv_next_row(input->parser) != zsv_status_row)
          input->done = 1;
        else {
          if((input->col_count = zsv_cell_count(input->parser)))
            if(!(input->output_colnames = calloc(input->col_count, sizeof(*input->output_colnames))))
              data->status = zsv_compare_status_memory;
        }

        for(unsigned j = 0; j < input->col_count && !input->done
              && data->status == zsv_compare_status_ok; j++) {
          struct zsv_cell colname = zsv_get_cell(input->parser, j);
          const unsigned char *colname_s = colname.str;
          unsigned colname_len = colname.len;
          int added = 0;
          unsigned instance_num = 0;
          zsv_compare_unique_colname *input_col =
            zsv_compare_unique_colname_add_if_not_found(&input->colnames,
                                                        colname_s, colname_len,
                                                        instance_num, &added);
          if(!input_col) {
            data->status = zsv_compare_status_error;
            break;
          }

          if(!added) { // we've seen this column before in this input
            if(!data->allow_duplicate_column_names) { // no dupes; ignore this col
              input_col = NULL;
            } else { // update instance num and retry
              instance_num = ++input_col->instance_num;
              input_col =
                zsv_compare_unique_colname_add_if_not_found(&input->colnames,
                                                            colname_s, colname_len,
                                                            instance_num, &added);
              if(!added) // should not happen
                data->status = zsv_compare_status_error;
            }
          }

          if(input_col && added) {
            // now that we know this colname+instance_num is unique to this input
            // add it to the output
            zsv_compare_unique_colname *output_col =
              zsv_compare_unique_colname_add_if_not_found(&data->output_colnames,
                                                          colname_s, colname_len,
                                                          instance_num, &added);
            if(!output_col) // error
              data->status = zsv_compare_status_error;
            else {
              if(added) {
                *data->output_colnames_next = output_col;
                data->output_colnames_next = &output_col->next;
                output_col->output_ix = data->output_colcount++;
              }
              input->output_colnames[j] = output_col;
            }
          }
        }
      }

      if(data->output_colcount == 0)
        data->status = zsv_compare_status_no_data;

      // assign out2in mappings
      for(unsigned i = 0; data->status == zsv_compare_status_ok && i < data->input_count; i++) {
        struct zsv_compare_input *input = &data->inputs[i];
        if(input->done)
          continue;
        if(!(input->out2in = calloc(data->output_colcount, sizeof(*input->out2in))))
          data->status = zsv_compare_status_memory;
        else {
          for(unsigned j = 0; j < input->col_count; j++) {
            zsv_compare_unique_colname *output_col = input->output_colnames[j];
            if(output_col)
              input->out2in[output_col->output_ix] = j + 1;
          }
        }
      }
    }

    // assertions
    if(data->status == zsv_compare_status_ok) {
      int ok = 0;
      for(unsigned i = 0; i < data->input_count; i++)
        if(!data->inputs[i].done)
          ok++;

      if(ok < 2) {
        fprintf(stderr, "Compare requires at least two non-empty inputs\n");
        data->status = zsv_compare_status_error;
      }
    }

    // next, compare each row
    while(data->status == zsv_compare_status_ok && zsv_compare_next(data) == zsv_compare_status_ok)
      ;

    if(started)
      zsv_compare_end(data);
  }

  free(input_filenames);

  enum zsv_compare_status stat = data->status;
  zsv_compare_delete(data);
  return stat;
  /*
  int err = 0;
  for(int i = 1; !err && i < argc; i++) {
    const char *arg = argv[i];
    if(!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      count_usage();
      goto count_done;
    } if(!strcmp(arg, "-i") || !strcmp(arg, "--input") || *arg != '-') {
      err = 1;
      if((!strcmp(arg, "-i") || !strcmp(arg, "--input")) && ++i >= argc)
        fprintf(stderr, "%s option requires a filename\n", arg);
      else {
        if(opts->stream)
          fprintf(stderr, "Input may not be specified more than once\n");
        else if(!(opts->stream = fopen(argv[i], "rb")))
          fprintf(stderr, "Unable to open for reading: %s\n", argv[i]);
        else {
          input_path = argv[i];
          err = 0;
        }
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

#ifdef NO_STDIN
  if(!opts->stream || opts->stream == stdin) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }
#endif

  if(!err) {
    opts->row_handler = row;
    opts->ctx = &data;
    if(zsv_new_with_properties(opts, input_path, opts_used, &data.parser) != zsv_status_ok) {
      fprintf(stderr, "Unable to initialize parser\n");
      err = 1;
    } else {
      enum zsv_status status;
      while((status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;
      zsv_finish(data.parser);
      zsv_delete(data.parser);
      printf("%zu\n", data.rows  > 0 ? data.rows - 1 : 0);
    }
  }

 count_done:
  if(opts->stream && opts->stream != stdin)
    fclose(opts->stream);

  return err;
*/
}
