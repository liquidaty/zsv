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
#include <math.h>
#include <jsonwriter.h>

#include <sqlite3.h>
extern sqlite3_module CsvModule;

#include <zsv/utils/string.h>
#include <zsv/utils/writer.h>

#define ZSV_COMMAND compare
#include "zsv_command.h"

#include "compare.h"
#include "compare_internal.h"

#include "compare_unique_colname.c"
#include "compare_added_column.c"
#include "compare_sort.c"

#define ZSV_COMPARE_OUTPUT_TYPE_JSON 'j'

static struct zsv_compare_key **zsv_compare_key_add(struct zsv_compare_key **next,
                                                    const char *s, int *err) {
  struct zsv_compare_key *k = calloc(1, sizeof(*k));
  if(!k)
    *err = 1;
  else {
    k->name = s;
    *next = k;
    next = &k->next;
  }
  return next;
}

static void zsv_compare_output_property_name(struct zsv_compare_data *data, int new_row, char skip) {
  if(new_row)
    data->writer.cell_ix = 0;
  else
    data->writer.cell_ix++;
  if(!skip) {
    if(data->writer.cell_ix < data->writer.properties.used)
      jsonwriter_object_key(data->writer.handle.jsw, data->writer.properties.names[data->writer.cell_ix]);
    else
      jsonwriter_object_key(data->writer.handle.jsw, "Error missing key!");
  }
}

static void zsv_compare_output_strn(struct zsv_compare_data *data,
                                    const unsigned char *s, size_t len,
                                    int new_row,
                                    int quoted) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON) {
    if(data->writer.object && s == NULL) {
      zsv_compare_output_property_name(data, new_row, 1);
      return;
    }
    if(data->writer.object)
      zsv_compare_output_property_name(data, new_row, 0);
    if(s == NULL)
      jsonwriter_null(data->writer.handle.jsw);
    else
      jsonwriter_strn(data->writer.handle.jsw, s, len);
  } else {
    if(s == NULL)
      zsv_writer_cell_blank(data->writer.handle.csv, ZSV_WRITER_SAME_ROW);
    else
      zsv_writer_cell(data->writer.handle.csv, new_row, s, len, quoted);
  }
}

static void zsv_compare_output_str(struct zsv_compare_data *data,
                                   const unsigned char *s,
                                   int new_row,
                                   int quoted) {
  zsv_compare_output_strn(data, s, s ? strlen((const char *)s) : 0, new_row, quoted);
}

static void zsv_compare_output_zu(struct zsv_compare_data *data,
                                  size_t n,
                                  int new_row) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON) {
    if(data->writer.object)
      zsv_compare_output_property_name(data, new_row, 0);
    jsonwriter_int(data->writer.handle.jsw, n);
  } else
    zsv_writer_cell_zu(data->writer.handle.csv, ZSV_WRITER_NEW_ROW, data->row_count);
}

static void zsv_compare_header_str(struct zsv_compare_data *data,
                                   const unsigned char *s,
                                   int new_row,
                                   int quoted) {
  if(!(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON &&
       data->writer.object))
    zsv_compare_output_str(data, s, new_row, quoted);
  else {
    // we will output as JSON objects, so save the property names for later use
    if(data->writer.properties.used + 1 < data->writer.properties.allocated)
      data->writer.properties.names[data->writer.properties.used++] = strdup(s ? (const char *)s : "");
    else
      fprintf(stderr, "zsv_compare_header_str: insufficient header names allocation adding %s!\n", s);
  }
}

static void zsv_compare_allocate_properties(struct zsv_compare_data *data,
                                            unsigned count) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON &&
     data->writer.object && count > 0) {
    if((data->writer.properties.names = malloc(count*sizeof(*data->writer.properties.names))))
      data->writer.properties.allocated = count;
  }
}

static void zsv_compare_json_row_start(struct zsv_compare_data *data) {
  if(data->writer.object)
    jsonwriter_start_object(data->writer.handle.jsw);
  else
    jsonwriter_start_array(data->writer.handle.jsw);
}

static void zsv_compare_json_row_end(struct zsv_compare_data *data) {
  if(data->writer.object)
    jsonwriter_end_object(data->writer.handle.jsw);
  else
    jsonwriter_end_array(data->writer.handle.jsw);
}

static void zsv_compare_output_tuple(struct zsv_compare_data *data,
                                     struct zsv_compare_input *key_input,
                                     const unsigned char *colname,
                                     struct zsv_cell *values, // in original input order
                                     char is_key
                                     ) {
  // print ID | Column | Value 1 | ... | Value N
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON)
    zsv_compare_json_row_start(data);

  // to do: output ID values
  if(!data->keys) // id is effectively just row number
    zsv_compare_output_zu(data, data->row_count, ZSV_WRITER_NEW_ROW);
  else {
    for(unsigned idx = 0; idx < key_input->key_count; idx++) {
      struct zsv_cell *c = &key_input->keys[idx].value;
      zsv_compare_output_strn(data, c->str, c->len, idx == 0 ? ZSV_WRITER_NEW_ROW : ZSV_WRITER_SAME_ROW, c->quoted);
    }
  }

  // output additional columns
  for(struct zsv_compare_added_column *ac = data->added_columns; ac; ac = ac->next) {
    if(!ac->input) {
      if(data->writer.type != ZSV_COMPARE_OUTPUT_TYPE_JSON)
        zsv_compare_output_str(data, NULL, ZSV_WRITER_SAME_ROW, 0);
    } else {
      struct zsv_cell c = data->get_cell(ac->input, ac->col_ix);
      zsv_compare_output_strn(data, c.str, c.len, ZSV_WRITER_SAME_ROW, c.quoted);
    }
  }

  // output column name of this cell
  zsv_compare_output_str(data, colname, ZSV_WRITER_SAME_ROW, 1);

  for(unsigned i = 0; i < data->input_count; i++) {
    struct zsv_compare_input *input = &data->inputs[i];
    if((input->done || !input->row_loaded) && !is_key) { // no data for this input
      zsv_compare_output_str(data, NULL, ZSV_WRITER_SAME_ROW, 0);
    } else {
      struct zsv_cell *value = &values[i];
      zsv_compare_output_strn(data, value->str, value->len, ZSV_WRITER_SAME_ROW, value->quoted);
    }
  }

  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON)
    zsv_compare_json_row_end(data);
}

static const unsigned char *zsv_compare_combined_key_names(struct zsv_compare_data *data) {
  if(!data->combined_key_names) {
    size_t len = 2;

    for(unsigned key_ix = 0; key_ix < data->key_count; key_ix++) {
      struct zsv_compare_key *key = &data->keys[key_ix];
      if(key && key->name)
        len += strlen(key->name) + 1;
    }
    if((data->combined_key_names = calloc(1, len))) {
      unsigned char *start = NULL;
      for(unsigned key_ix = 0; key_ix < data->key_count; key_ix++) {
        struct zsv_compare_key *key = &data->keys[key_ix];
        if(key && key->name) {
          if(start) {
            *start = (unsigned char)'|';
            start++;
          } else
            start = data->combined_key_names;
          strcpy((char *)start, key->name);
          start += strlen((char *)start);
        }
      }
    }
  }
  return data->combined_key_names;
}

static void zsv_compare_print_row(struct zsv_compare_data *data,
                                  const unsigned last_ix   // last input ix in inputs_to_sort
                                  ) {
  struct zsv_compare_input *key_input = data->inputs_to_sort[0];

  // for now, output format is simple: for each value,
  // output a single scalar if the values are the same,
  // and a tuple if they differ
  struct zsv_cell *values = calloc(data->input_count, sizeof(*values));
  if(!values) {
    data->status = zsv_compare_status_memory;
    return;
  }

#define ZSV_COMPARE_MISSING "Missing"

  // if we don't have data from every input, then output "Missing" for missing inputs
  char got_missing = 0;
  for(unsigned i = 0; i < data->input_count; i++) {
    struct zsv_compare_input *input = data->inputs_to_sort[i];
    if(i > last_ix) {
      got_missing = 1;
      unsigned input_ix = input->index;
      values[input_ix].str = (unsigned char *)ZSV_COMPARE_MISSING;
      values[input_ix].len = strlen(ZSV_COMPARE_MISSING);
    }
  }
  if(got_missing) {
    const unsigned char *key_names = data->print_key_col_names ? zsv_compare_combined_key_names(data) : (const unsigned char *)"<key>";
    zsv_compare_output_tuple(data, key_input, key_names, values, 1);
    // reset values
    memset(values, 0, data->input_count * sizeof(*values));
  }

  // for each output column
  zsv_compare_unique_colname *output_col = data->output_colnames_first;
  for(unsigned output_ix = 0; output_ix < data->output_colcount && output_col != NULL;
      output_ix++, output_col = output_col->next) {
    if(output_col->is_key)
      continue;

    char different = 0;
    unsigned first_input_ix = 0;
    for(unsigned i = 0; i <= last_ix; i++) {
      struct zsv_compare_input *input = data->inputs_to_sort[i];
      if(input->done || !input->row_loaded) continue;

      unsigned input_ix = input->index;
      if(i == 0)
        first_input_ix = input_ix;

      unsigned col_ix_plus_1 = input->out2in[output_ix];
      if(col_ix_plus_1 == 0)
        values[input_ix].len = 0;
      else {
        unsigned input_col_ix = col_ix_plus_1 - 1;
        if(!output_col)
          output_col = input->output_colnames[input_col_ix];
        values[input_ix] = data->get_cell(input, input_col_ix);
        if(i > 0 && !different && data->cmp(data->cmp_ctx, values[first_input_ix], values[input_ix], data, input_col_ix)) {
          different = 1;
          if(data->tolerance.value
             && values[first_input_ix].len < ZSV_COMPARE_MAX_NUMBER_BUFF_LEN
             && values[input_ix].len < ZSV_COMPARE_MAX_NUMBER_BUFF_LEN) {
            // check if both are numbers with a difference less than the given tolerance            
            double d1, d2;
            memcpy(data->tolerance.str1, values[first_input_ix].str, values[first_input_ix].len);
            data->tolerance.str1[values[first_input_ix].len] = '\0';
            memcpy(data->tolerance.str2, values[input_ix].str, values[input_ix].len);
            data->tolerance.str2[values[input_ix].len] = '\0';
            if(!zsv_strtod_exact(data->tolerance.str1, &d1)
               && !zsv_strtod_exact(data->tolerance.str2, &d2)
               && fabs(d1 - d2) < data->tolerance.value)
              different = 0;
          }
        }
      }
    }

    if(different)
      zsv_compare_output_tuple(data, key_input, output_col->name, values, 0);
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
  free(input->keys);
  if(input->sort_stmt) {
    sqlite3_finalize(input->sort_stmt);
  }
}

static enum zsv_compare_status zsv_compare_set_inputs(struct zsv_compare_data *data, unsigned input_count) {
  if(!input_count || !(data->inputs = calloc(input_count, sizeof(*data->inputs)))
     || !(data->inputs_to_sort = calloc(input_count, sizeof(*data->inputs_to_sort))))
    return zsv_compare_status_memory;
  data->input_count = input_count;
  for(unsigned i = 0; i < input_count; i++) {
    struct zsv_compare_input *input = &data->inputs[i];
    input->index = i;
    data->inputs_to_sort[i] = input;
    if(data->key_count) {
      if(!(input->keys = calloc(data->key_count, sizeof(*input->keys))))
        return zsv_compare_status_memory;

      input->key_count = data->key_count;
      unsigned j = 0;
      for(struct zsv_compare_key *key = data->keys; key; key = key->next)
        input->keys[j++].key = key;
    }
  }
  return zsv_compare_status_ok;
}

static int zsv_compare_cell(void *ctx, struct zsv_cell c1, struct zsv_cell c2,
                            void *data, unsigned col_ix);

static void zsv_compare_output_begin(struct zsv_compare_data *data) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON) {
    if(!(data->writer.handle.jsw = jsonwriter_new(stdout))) // to do: data->out
      data->status = zsv_compare_status_memory;
    else {
      if(data->writer.compact)
        jsonwriter_set_option(data->writer.handle.jsw, jsonwriter_option_compact);
      jsonwriter_start_array(data->writer.handle.jsw);
    }
  } else {
    if(!(data->writer.handle.csv = zsv_writer_new(NULL)))
      data->status = zsv_compare_status_memory;
  }

  if(data->status == zsv_compare_status_ok) {
    unsigned header_col_count =
      (data->key_count ? data->key_count : 1) + // match keys
      2 + // column name and column value
      data->input_count + // input names
      data->added_colcount; // added columns

    zsv_compare_allocate_properties(data, header_col_count);

    // write header row
    if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON && !data->writer.object)
      jsonwriter_start_array(data->writer.handle.jsw);

    // write keys
    if(!data->keys) // id is effectively just row number
      zsv_compare_header_str(data, (const unsigned char *)"Row #", ZSV_WRITER_NEW_ROW, 0);
    else {
      for(struct zsv_compare_key *key_name = data->keys; key_name; key_name = key_name->next)
        zsv_compare_header_str(data,
                               (const unsigned char *)key_name->name,
                               key_name == data->keys ? ZSV_WRITER_NEW_ROW : ZSV_WRITER_SAME_ROW,
                               1);
    }

    // write additional column names
    for(struct zsv_compare_added_column *ac = data->added_columns; ac; ac = ac->next)
      zsv_compare_header_str(data, ac->colname->name, ZSV_WRITER_SAME_ROW, 1);

    // write "Column"
    zsv_compare_header_str(data, (const unsigned char *)"Column", ZSV_WRITER_SAME_ROW, 0);

    // write input name(s)
    for(unsigned i = 0; i < data->input_count; i++) {
      struct zsv_compare_input *input = &data->inputs[i];
      zsv_compare_header_str(data, (const unsigned char *)input->path, ZSV_WRITER_SAME_ROW, 1);
    }

    if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON && !data->writer.object)
      jsonwriter_end_array(data->writer.handle.jsw);
  }
}

static void zsv_compare_output_end(struct zsv_compare_data *data) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON) {
    if(data->writer.handle.jsw)
      jsonwriter_end(data->writer.handle.jsw);
  } else {
    zsv_writer_flush(data->writer.handle.csv);
  }
  if(data->status == zsv_compare_status_no_more_input)
    data->status = zsv_compare_status_ok;
}

static enum zsv_status zsv_compare_next_unsorted_row(struct zsv_compare_input *input) {
  return zsv_next_row(input->parser);
}

static struct zsv_cell zsv_compare_get_unsorted_cell(struct zsv_compare_input *input,
                                                     unsigned ix) {
  return zsv_get_cell_trimmed(input->parser, ix);
}

static unsigned zsv_compare_get_unsorted_colcount(struct zsv_compare_input *input) {
  return zsv_cell_count(input->parser);
}

static enum zsv_compare_status
input_init_unsorted(struct zsv_compare_data *data,
                    struct zsv_compare_input *input,
                    struct zsv_opts *opts,
                    struct zsv_prop_handler *custom_prop_handler,
                    const char *opts_used) {
  (void)(opts_used);
  if(!(input->stream = fopen(input->path, "rb"))) {
    perror(input->path);
    return zsv_compare_status_error;
  }
  struct zsv_opts these_opts = *opts;
  these_opts.stream = input->stream;
  enum zsv_status stat = zsv_new_with_properties(&these_opts, custom_prop_handler, input->path, NULL, &input->parser);
  if(stat != zsv_status_ok)
    return zsv_compare_status_error;

  if(data->next_row(input) != zsv_status_row)
    return zsv_compare_status_error;

  return zsv_compare_status_ok;
}

zsv_compare_handle zsv_compare_new() {
  zsv_compare_handle z = calloc(1, sizeof(*z));
#if defined(ZSV_COMPARE_CMP_FUNC) && defined(ZSV_COMPARE_CMP_CTX)
  zsv_compare_set_comparison(z, ZSV_COMPARE_CMP_FUNC, ZSV_COMPARE_CMP_CTX);
#else
  zsv_compare_set_comparison(z, zsv_compare_cell, NULL);
#endif
  z->output_colnames_next = &z->output_colnames;

  z->next_row = zsv_compare_next_unsorted_row;
  z->get_cell = zsv_compare_get_unsorted_cell;
  z->get_column_name = zsv_compare_get_unsorted_cell;
  z->get_column_count = zsv_compare_get_unsorted_colcount;
  z->input_init = input_init_unsorted;
  return z;
}

static void zsv_compare_set_sorted_callbacks(struct zsv_compare_data *data) {
  data->next_row = zsv_compare_next_sorted_row;
  data->get_cell = zsv_compare_get_sorted_cell;
  data->get_column_name = zsv_compare_get_sorted_colname;
  data->get_column_count = zsv_compare_get_sorted_colcount;
  data->input_init = input_init_sorted;
}

static enum zsv_compare_status zsv_compare_init_sorted(struct zsv_compare_data *data) {
  int rc;
  const char *db_url = data->sort_in_memory ? "file::memory:" : "";
  if((rc = sqlite3_open_v2(db_url, &data->sort_db, SQLITE_OPEN_URI | SQLITE_OPEN_READWRITE, NULL)) == SQLITE_OK
     && data->sort_db
     && (rc = sqlite3_create_module(data->sort_db, "csv", &CsvModule, 0) == SQLITE_OK)) {
    zsv_compare_set_sorted_callbacks(data);
    return zsv_compare_status_ok;
  }
  return zsv_compare_status_error;
}

static void zsv_compare_data_free(struct zsv_compare_data *data) {
  if(data->writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON) {
    if(data->writer.handle.jsw)
      jsonwriter_delete(data->writer.handle.jsw);
  } else
    zsv_writer_delete(data->writer.handle.csv);

  for(unsigned i = 0; i < data->input_count; i++)
    zsv_compare_input_free(&data->inputs[i]);
  free(data->inputs);
  free(data->combined_key_names);
  free(data->inputs_to_sort);
  for(unsigned i = 0; i < data->writer.properties.used; i++)
    free(data->writer.properties.names[i]);
  free(data->writer.properties.names);

  if(data->sort) {
    if(data->sort_db)
      sqlite3_close(data->sort_db);
  }

  zsv_compare_added_column_delete(data->added_columns);

  zsv_compare_unique_colnames_delete(&data->output_colnames);
  zsv_compare_unique_colnames_delete(&data->added_colnames);

  for(struct zsv_compare_key *next, *key = data->keys; key; key = next) {
    next = key->next;
    free(key);
  }
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

static int zsv_compare_cell(void *ctx, struct zsv_cell c1, struct zsv_cell c2,
                            void *data, unsigned col_ix) {
  (void)(ctx);
  (void)(data);
  (void)(col_ix);
  return zsv_strincmp(c1.str, c1.len,
                      c2.str, c2.len);
}

static enum zsv_compare_status zsv_compare_advance(struct zsv_compare_data *data) {
  // advance each input (if not row_loaded) to their next row
  char got = 0;
  for(unsigned i = 0; i < data->input_count; i++) {
    struct zsv_compare_input *input = &data->inputs[i];
    if(input->done) continue;

    if(input->row_loaded) {
      got = 1;
      continue;
    }
    if(data->next_row(input) != zsv_status_row)
      input->done = 1;
    else {
      for(unsigned idx = 0; idx < input->key_count; idx++)
        input->keys[idx].value = data->get_cell(input, input->keys[idx].col_ix);
      input->row_loaded = 1;
      got = 1;
    }
  }
  return got ? zsv_compare_status_ok : zsv_compare_status_no_more_input;
}

static int zsv_compare_inputp_cmp(const void *inputpx, const void* inputpy) {
  struct zsv_compare_input * const *xp = inputpx;
  struct zsv_compare_input * const *yp = inputpy;
  const struct zsv_compare_input *x = *xp;
  const struct zsv_compare_input *y = *yp;

  if(!x->row_loaded && !y->row_loaded) return 0;
  if(!x->row_loaded) return 1;
  if(!y->row_loaded) return -1;

  int cmp = 0;
  for(unsigned i = 0; !cmp && i < x->key_count && i < y->key_count; i++)
    // for multibyte input, the input must be also sorted lexicographically
    // to avoid potential mismatches
    // see e.g. https://stackoverflow.com/questions/4611302/sorting-utf-8-strings
    cmp = zsv_strincmp(x->keys[i].value.str, x->keys[i].value.len,
                       y->keys[i].value.str, y->keys[i].value.len);
  return cmp;
}


static enum zsv_compare_status zsv_compare_next(struct zsv_compare_data *data) {
  data->status = zsv_compare_advance(data);
  if(data->status != zsv_compare_status_ok) return data->status;

  data->row_count++;
  // sort the inputs by ID value first, and input position second
  // for as many inputs have the same smallest ID values, output them as a group
  //   and set input->row_loaded to 0
  qsort(data->inputs_to_sort, data->input_count,
        sizeof(*data->inputs_to_sort), zsv_compare_inputp_cmp);

  // find the next subset of inputs with identical id values and process those inputs
  unsigned last = 0;
  struct zsv_compare_input *min_input = data->inputs_to_sort[0];
  for(unsigned tmp_i = 1; tmp_i < data->input_count; tmp_i++) {
    struct zsv_compare_input *tmp = data->inputs_to_sort[tmp_i];
    if(!tmp->row_loaded) continue;
    if(!zsv_compare_inputp_cmp(&min_input, &tmp)) { // keys are the same
      last = tmp_i;
      continue;
    }

    // keys are different
    break;
  }

  // print row
  zsv_compare_print_row(data, last);

  // reset row_loaded
  for(unsigned tmp = 0; tmp <= last; tmp++)
    data->inputs_to_sort[tmp]->row_loaded = 0;

  return zsv_compare_status_ok;
}

static int compare_usage() {
  static const char *usage[] = {
    "Usage: compare [options] [file1.csv] [file2.csv] [...]",
    "Options:",
    "  -h,--help          : show usage",
    "  -k,--key <colname> : specify a column to match rows on",
    "                       can be specified multiple times",
    "  -a,--add <colname> : specify an additional column to output",
    "                       will use the [first input] source",
    "  --sort             : sort on keys before comparing",
    "  --sort-in-memory   : for sorting,  use in-memory instead of temporary db",
    "                       (see https://www.sqlite.org/inmemorydb.html)",
    "  --tolerance <value>: ignore differences where both values are numeric",
    "                       strings with values differing by less than the given",
    "                       amount e.g. --tolerance 0.01 will ignore differences",
    "                       of numeric strings such as 123.45 vs 123.44",
    "  --json             : output as JSON",
    "  --json-compact     : output as compact JSON",
    "  --json-object      : output as an array of objects",
    "  --print-key-colname: when outputting key column diffs,",
    "                       print column name instead of <key>",
    "",
    "NOTES",
    "",
    "    If no keys are specified, each row from each input is compared to the",
    "    row in the corresponding position in each other input (all the first rows",
    "    from each input are compared to each other, all the second rows are compared to",
    "    each other, etc).",
    "",
    "    If one or more key is specified, each input is assumed to already be",
    "    lexicographically sorted in ascending order; this is a necessary condition",
    "    for the output to be correct (unless the --sort option is used). However, it",
    "    is not required for each input to contain the same population of row keys",
    "",
    "    The --sort option uses sqlite3 (unindexed) sort and is intended to be a",
    "    convenience rather than performance feature. If you need high performance",
    "    sorting, other solutions, such as a multi-threaded parallel sort, are likely",
    "    superior. For handling quoted data, `2tsv` can be used to convert to a delimited",
    "    format without quotes, that can be directly parsed with common UNIX utilities",
    "    (such as `sort`), and `select --unescape` can be used to convert back",
    NULL
  };

  for(unsigned i = 0; usage[i]; i++)
    printf("%s\n", usage[i]);
  printf("\n");
  return 0;
}

// TO DO: consolidate w sql.c, move common code to utils/db.c
int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  /**
   * See sql.c re passing options to sqlite3 when sorting is used
   */
  (void)(opts_used);
  if(argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    compare_usage();
    return argc < 2 ? 1 : 0;
  }

  // temporarily hold the input file names
  const char **input_filenames = calloc(argc, sizeof(*input_filenames));
  if(!input_filenames)
    return zsv_compare_status_memory;

  zsv_compare_handle data = zsv_compare_new();
  if(!data) {
    free(input_filenames);
    return zsv_compare_status_memory;
  }

  int err = 0;
  // initialization starts here. to do: make this a separate function
  unsigned input_count = 0;
  struct zsv_compare_key **next_key = &data->keys;
  struct zsv_compare_added_column **added_column_next = &data->added_columns;
  for(int arg_i = 1; data->status == zsv_compare_status_ok
        && !err && arg_i < argc; arg_i++) {
    const char *arg = argv[arg_i];
#include <zsv/utils/arg.h>
    if(!strcmp(arg, "-k") || !strcmp(arg, "--key")) {
      const char *next_arg = zsv_next_arg(++arg_i, argc, argv, &err);
      if(next_arg) {
        next_key = zsv_compare_key_add(next_key, next_arg, &err);
        data->key_count++;
      }
    } else if(!strcmp(arg, "-a") || !strcmp(arg, "--add")) {
      const char *next_arg = zsv_next_arg(++arg_i, argc, argv, &err);
      if(next_arg) {
        zsv_compare_unique_colname *colname;
        if((data->status =
            zsv_compare_unique_colname_add(&data->added_colnames,
                                           (const unsigned char *)next_arg,
                                           strlen(next_arg), &colname))
           == zsv_compare_status_ok) {
          // add to linked list for use after all data->output_colnames are allocated
          added_column_next =
            zsv_compare_added_column_add(added_column_next, colname,
                                         &data->status);
          if(data->status == zsv_compare_status_ok)
            data->added_colcount++;
        }
      }
    } else if(!strcmp(arg, "--tolerance")) {
      const char *next_arg = zsv_next_arg(++arg_i, argc, argv, &err);
      if(next_arg) {
        if(zsv_strtod_exact(next_arg, &data->tolerance.value))
          fprintf(stderr, "Invalid numeric value: %s\n", next_arg), err = 1;
        else if(data->tolerance.value < 0)
          fprintf(stderr, "Tolerance must be greater than zero (got %s)\n", next_arg), err = 1;
        else
          data->tolerance.value = nextafterf(data->tolerance.value, INFINITY);
      }
    } else if(!strcmp(arg, "--sort")) {
      data->sort = 1;
    } else if(!strcmp(arg, "--json")) {
      data->writer.type = ZSV_COMPARE_OUTPUT_TYPE_JSON;
    } else if(!strcmp(arg, "--json-object")) {
      data->writer.type = ZSV_COMPARE_OUTPUT_TYPE_JSON;
      data->writer.object = 1;
    } else if(!strcmp(arg, "--json-compact")) {
      data->writer.type = ZSV_COMPARE_OUTPUT_TYPE_JSON;
      data->writer.compact = 1;
    } else if(!strcmp(arg, "--print-key-colname")) {
      data->print_key_col_names = 1;
    } else
      input_filenames[input_count++] = arg;
  }

  struct zsv_opts original_default_opts;
  struct zsv_prop_handler original_default_custom_prop_handler;
  if(data->sort) {
    if(!data->key_count) {
      fprintf(stderr, "Error: --sort requires one or more keys\n");
      data->status = zsv_compare_status_error;
    } else {
      original_default_opts = zsv_get_default_opts();
      zsv_set_default_opts(*opts);

      if(custom_prop_handler) {
        original_default_custom_prop_handler = zsv_get_default_custom_prop_handler();
        zsv_set_default_custom_prop_handler(*custom_prop_handler);
      }

      if(data->status == zsv_compare_status_ok)
        data->status = zsv_compare_init_sorted(data);
    }
  }

  if(err && data->status == zsv_compare_status_ok)
    data->status = zsv_compare_status_error;
  else if(!input_count)
    data->status = zsv_compare_status_error;
  else if(data->status == zsv_compare_status_ok) {
    if((data->status = zsv_compare_set_inputs(data, input_count)) == zsv_compare_status_ok) {
      // initialize parsers
      for(unsigned ix = 0; data->status == zsv_compare_status_ok && ix < input_count; ix++) {
        struct zsv_compare_input *input = &data->inputs[ix];
        input->path = input_filenames[ix];
        data->status = data->input_init(data, input, opts, custom_prop_handler, opts_used);
      }
    }

    if(data->status == zsv_compare_status_ok) {
      // find keys
      for(unsigned i = 0; data->status == zsv_compare_status_ok && i < data->input_count; i++) {
        struct zsv_compare_input *input = &data->inputs[i];
        if((input->col_count = data->get_column_count(input))) {
          if(!(input->output_colnames = calloc(input->col_count, sizeof(*input->output_colnames)))) {
            data->status = zsv_compare_status_memory;
            break;
          }
        }

        unsigned found_keys = 0;
        for(unsigned j = 0; j < input->col_count && !input->done
              && data->status == zsv_compare_status_ok; j++) {
          struct zsv_cell colname = data->get_column_name(input, j);
          const unsigned char *colname_s = colname.str;
          unsigned colname_len = colname.len;
          zsv_compare_unique_colname *input_col;
          data->status =
            zsv_compare_unique_colname_add(&input->colnames,
                                           colname_s, colname_len,
                                           &input_col);
          if(data->status != zsv_compare_status_ok)
            break;

          if(input_col) {
            // now that we know this colname+instance_num is unique to this input
            // check if it is a key
            for(unsigned key_ix = 0; found_keys < input->key_count && key_ix < input->key_count; key_ix++) {
              struct zsv_compare_input_key *k = &input->keys[key_ix];
              if(!k->found && !zsv_strincmp(colname_s, colname_len, (const unsigned char *)k->key->name, strlen(k->key->name))) {
                k->found = 1;
                found_keys++;
                k->col_ix = j;
                input_col->is_key = 1;
                break;
              }
            }

            // add it to the output
            int added = 0;
            zsv_compare_unique_colname *output_col =
              zsv_compare_unique_colname_add_if_not_found(&data->output_colnames,
                                                          colname_s, colname_len,
                                                          input_col->instance_num, &added);
            if(!output_col) // error
              data->status = zsv_compare_status_error;
            else {
              if(added) {
                if(*data->output_colnames_next)
                  (*data->output_colnames_next)->next = output_col;
                if(!data->output_colnames_first)
                  data->output_colnames_first = output_col;

                *data->output_colnames_next = output_col;
                output_col->is_key = input_col->is_key;
                data->output_colnames_next = &output_col->next;
                output_col->output_ix = data->output_colcount++;
              }
              input->output_colnames[j] = output_col;
            }
          }
        }

        if(found_keys != data->key_count) {
          fprintf(stderr, "Unable to find the following keys in %s: ", input->path);
          for(unsigned int j = 0; j < input->key_count; j++) {
            struct zsv_compare_input_key *k = &input->keys[j];
            if(!k->found)
              fprintf(stderr, "\n  %s", k->key->name);
          }
          fprintf(stderr, "\n");
          data->status = zsv_compare_status_error;
        }
      }
    }

    if(data->status == zsv_compare_status_ok) {
      if(data->output_colcount == 0)
        data->status = zsv_compare_status_no_data;
    }

    char started = 0;
    if(data->status == zsv_compare_status_ok) {
      started = 1;
      zsv_compare_output_begin(data);

      // match output colnames to added columns
      for(struct zsv_compare_added_column *ac = data->added_columns;
          ac; ac = ac->next) {
        zsv_compare_unique_colname col = { 0 };
        col.name = ac->colname->name;
        col.name_len = ac->colname->name_len;
        col.instance_num = ac->colname->instance_num;
        ac->output_colname = sglib_zsv_compare_unique_colname_find_member(data->output_colnames, &col);
        if(!ac->output_colname)
          fprintf(stderr, "Warning: added column %.*s not found in any input\n", (int)col.name_len, col.name);
      }

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
            if(output_col) {
              input->out2in[output_col->output_ix] = j + 1;

              // check if this should be the source of any additional columns
              for(struct zsv_compare_added_column *ac = data->added_columns;
                  ac; ac = ac->next) {
                if(!ac->input && ac->output_colname) {
                  if(output_col == ac->output_colname) {
                    ac->input = input;
                    ac->col_ix = j;
                  }
                }
              }
            }
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
      zsv_compare_output_end(data);
  }

  free(input_filenames);

  err = data->status == zsv_compare_status_ok ? 0 : 1;

  if(data->sort) {
    zsv_set_default_opts(original_default_opts); // restore default options
    if(custom_prop_handler)
      zsv_set_default_custom_prop_handler(original_default_custom_prop_handler);
  }

  zsv_compare_delete(data);
  return err;
}
