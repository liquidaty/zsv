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

#include <unistd.h> // unlink

#define ZSV_COMMAND flatten
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/file.h>
#include <zsv/utils/utf8.h>
#include <zsv/utils/mem.h>
#include <zsv/utils/string.h>

enum flatten_agg_method {
  flatten_agg_method_none = 1,
  flatten_agg_method_array
};

struct flatten_column_name_and_ix {
  unsigned char *name;
  size_t name_len;
  unsigned int ix_plus_1;
  unsigned char free_name:1;
  unsigned char	dummy:7;
};

struct chars_list {
  struct chars_list *next;
  unsigned char *value;
};

static struct chars_list *chars_list_new(const unsigned char *utf8_value, size_t len) {
  struct chars_list *e = calloc(1, sizeof(*e));
  if(e)
    e->value = zsv_memdup(utf8_value, len);
  return e;
}

#ifndef FREEIF
#define FREEIF(x) if(x) free(x), x = NULL
#endif

static void chars_lists_delete(struct chars_list **p) {
  if(p && *p) {
    struct chars_list *next;
    for(struct chars_list *e = *p; e; e = next) {
      next = e->next;
      FREEIF(e->value);
      free(e);
    }
    *p = NULL;
  }
}

struct flatten_agg_col {
  struct flatten_agg_col *next;
  struct flatten_column_name_and_ix column;
  struct chars_list *values, **last_value;
  enum flatten_agg_method agg_method;
  unsigned char *delimiter;
};

struct flatten_agg_col_iterator {
  unsigned char *str;
  size_t len;

  // internal use only
  struct chars_list *current_cl;
};

static void flatten_agg_col_iterator_init(struct flatten_agg_col *c,
                                   struct flatten_agg_col_iterator *i) {
  memset(i, 0, sizeof(*i));
  switch(c->agg_method) {
  case flatten_agg_method_array:
    if((i->current_cl = c->values))
      i->str = i->current_cl->value;
    break;
  default:
    break;
  }
}

static void flatten_agg_col_iterator_replace_str(struct flatten_agg_col_iterator *i, unsigned char **new_s) {
  if(i->current_cl)
    i->current_cl->value = *new_s;
  else {
    fprintf(stderr, "flatten_agg_col_iterator_replace_str() error: no current value to replace\n");
    free(*new_s);
    *new_s = NULL;
  }
}

static void flatten_agg_col_iterator_next(struct flatten_agg_col_iterator *i) {
  if(i->current_cl && (i->current_cl = i->current_cl->next))
    i->str = i->current_cl->value;
}

static char flatten_agg_col_iterator_done(struct flatten_agg_col_iterator *i) {
  return i->current_cl ? 0 : 1;
}

static const unsigned char *flatten_agg_col_delimiter(struct flatten_agg_col *c) {
  if(c->delimiter)
    return c->delimiter;
  switch(c->agg_method) {
  case flatten_agg_method_none:
  case flatten_agg_method_array:
    return (const unsigned char *)"|";
  }
  return (const unsigned char *)"|";
}

static void flatten_agg_col_add_value(struct flatten_agg_col *c, const unsigned char *utf8_value, size_t len) {
  if(!c->last_value)
    c->last_value = &c->values;
  struct chars_list *e = chars_list_new(utf8_value, len);
  if(e) {
    *c->last_value = e;
    c->last_value = &e->next;
  }
}

typedef struct flatten_output_column {
  struct flatten_output_column *next;
  unsigned char *name;
  size_t name_len;
  unsigned char *compare_name; // same as name, unless case-insensitive in which case, lower case
  unsigned char *current_value;

  struct flatten_output_column *left;
  struct flatten_output_column *right;
  unsigned char color:1;
  unsigned char dummy:7;
} flatten_output_column;

void flatten_output_column_free(struct flatten_output_column *e) {
  FREEIF(e->name);
  FREEIF(e->compare_name);
  FREEIF(e->current_value);
}

int flatten_output_column_compare(flatten_output_column *x, flatten_output_column *y) {
  return strcmp((char *)x->compare_name, (char *)y->compare_name);
}

SGLIB_DEFINE_RBTREE_PROTOTYPES(flatten_output_column, left, right, color, flatten_output_column_compare);

SGLIB_DEFINE_RBTREE_FUNCTIONS(flatten_output_column, left, right, color, flatten_output_column_compare);

struct flatten_data {
  unsigned int max_cols;
  unsigned int output_column_total_count;

  struct flatten_output_column *output_columns_by_value;

  // output_columns_by_value, linked list
  struct flatten_output_column *output_columns_by_value_head;
  struct flatten_output_column **output_columns_by_value_tail;

  unsigned int current_column_index;
  unsigned int row_count;
  unsigned int row_count2;
  unsigned int output_row;

  struct flatten_column_name_and_ix row_id_column;
  struct flatten_column_name_and_ix column_name_column;
  struct flatten_column_name_and_ix value_column;

  struct flatten_output_column *current_column_name_column;
  unsigned char *current_column_name_value;

  unsigned char *last_asset_id;
  size_t last_asset_id_len;
  unsigned char *current_asset_id; // will equal last_asset_id if they are the same
  size_t current_asset_id_len;

  const char *output_filename;

  FILE *in;
  const char *input_path;
  FILE *out;

  zsv_csv_writer csv_writer;

  struct flatten_agg_col *agg_output_cols;
  struct flatten_agg_col **agg_output_cols_vector;
  unsigned int agg_output_cols_vector_size;

  int max_rows_per_aggregation;

  enum flatten_agg_method all_aggregation_method;

  unsigned char cancelled:1;
  unsigned char verbose:1;
  unsigned char have_agg:1;
  unsigned char dummy:5;
};

static int flatten_output_column_add(struct flatten_data *data,
                                     const unsigned char *utf8_value, size_t len,
                                     unsigned char *compare_name
                                     ) {
  if(data->output_column_total_count == data->max_cols) {
    free(compare_name);
    return zsv_printerr(1, "ERROR: Maximum number of columns (%i) exceeded", data->max_cols);
  }

  struct flatten_output_column *new_output_column = calloc(1, sizeof(*new_output_column));
  new_output_column->name = zsv_memdup(utf8_value, len);
  new_output_column->name_len = len;
  new_output_column->compare_name = compare_name;

  // add to rbtree
  sglib_flatten_output_column_add(&data->output_columns_by_value, new_output_column);

  // also add to linked list
  *data->output_columns_by_value_tail = new_output_column;
  data->output_columns_by_value_tail = &new_output_column->next;
  data->output_column_total_count++;
  return 0;
}

static flatten_output_column *flatten_output_column_find(struct flatten_data *data,
                                                         const unsigned char *utf8_value,
                                                         size_t len,
                                                         unsigned char **compare_name
                                                         ) {
  flatten_output_column node, *found;
  node.compare_name = zsv_strtolowercase(utf8_value, &len);
  if(node.compare_name) {
    if((found = sglib_flatten_output_column_find_member(data->output_columns_by_value, &node))) {
      free(node.compare_name);
      return found;
    }
    // not found
    if(compare_name)
      *compare_name = node.compare_name;
    else
      free(node.compare_name);
  }
  return NULL;
}

static void set_cnx(struct flatten_column_name_and_ix *cnx, const unsigned char *utf8_value, size_t len, unsigned int current_column_ix) {
  if(!cnx->ix_plus_1) {
    if(!cnx->name) { // none provided, assume its the next column
      if((cnx->name = zsv_memdup(utf8_value, len))) {
        cnx->free_name = 1;
        cnx->name_len = len;
      }
      cnx->ix_plus_1 = current_column_ix + 1;
    } else if(!zsv_strincmp(cnx->name, len, utf8_value, len))
      cnx->ix_plus_1 = current_column_ix + 1;
  }
}

// flatten_cell1(): for any value in the "column name" column, add it to the list of columns
static void flatten_cell1(void *hook, unsigned char *utf8_value, size_t len) {
  struct flatten_data *data = hook;
  if(!data->cancelled) {
    if(data->row_count == 0) {
      struct flatten_column_name_and_ix *cnxlist[] =
        {
         &data->row_id_column,
         &data->column_name_column,
         &data->value_column
        };
      for(unsigned int i = 0; i < 3; i++)
        if(cnxlist[i]->name || (!data->have_agg && i == data->current_column_index))
          set_cnx(cnxlist[i], utf8_value, len, data->current_column_index);
    } else if(data->current_column_index + 1 == data->column_name_column.ix_plus_1) {
      // we are in the "column name" column, so make sure we've added this to our columns to output
      unsigned char *compare_name = NULL;
      if(!flatten_output_column_find(data, utf8_value, len, &compare_name) && compare_name)
        data->cancelled = flatten_output_column_add(data, utf8_value, len, compare_name);
    }
  }
  data->current_column_index++;
}

static void flatten_row1(void *hook) {
  struct flatten_data *data = hook;
  if(data->cancelled)
    return;
  data->row_count++;
  data->current_column_index = 0;
}

static void flatten_cell2(void *hook, unsigned char *utf8_value, size_t len) {
  struct flatten_data *data = hook;
  if(!data->cancelled) {
    if(data->row_count2 == 0) {
      if(!data->row_id_column.ix_plus_1)
        if(data->row_id_column.name || !data->have_agg)
          set_cnx(&data->row_id_column, utf8_value, len, data->current_column_index);

      for(struct flatten_agg_col *c = data->agg_output_cols; c; c = c->next) {
        if(c->column.name_len == len
           && !zsv_strincmp(c->column.name, len, utf8_value, len))
          c->column.ix_plus_1 = data->current_column_index + 1;
      }
    } else {
      if(data->current_column_index < data->agg_output_cols_vector_size) {
        struct flatten_agg_col *c = data->agg_output_cols_vector[data->current_column_index];
        if(c)
          flatten_agg_col_add_value(c, utf8_value, len);
      }

      if(data->current_column_index + 1 == data->column_name_column.ix_plus_1) // column name
        data->current_column_name_column = flatten_output_column_find(data, utf8_value, len, NULL);

      else if(data->current_column_index + 1 == data->value_column.ix_plus_1) // value
        data->current_column_name_value = zsv_memdup(utf8_value, len);

      else if(data->current_column_index + 1 == data->row_id_column.ix_plus_1) { // asset ID
        if(!data->last_asset_id) { // no prior asset, so this is the first one
          data->last_asset_id = data->current_asset_id = zsv_memdup(utf8_value, len);
          data->last_asset_id_len = len;
        } else if(len != data->last_asset_id_len || memcmp(data->last_asset_id, utf8_value, len)) {
          // this is a different asset from the last one
          data->current_asset_id = zsv_memdup(utf8_value, len);
          data->current_asset_id_len = len;
        } else { // same as last asset
          data->current_asset_id = data->last_asset_id;
          data->current_asset_id_len = data->last_asset_id_len;
        }
      }
    }
  }
  data->current_column_index++;
}

static void flatten_output_header(struct flatten_data *data) {
  zsv_writer_cell(data->csv_writer, 1, data->row_id_column.name,
                    data->row_id_column.name_len, 1);
  unsigned int i = 1;
  for(struct flatten_output_column *col = data->output_columns_by_value_head;
      col; col = col->next, i++) {
    zsv_writer_cell(data->csv_writer, 0, col->name, col->name_len, 1);
  }

  for(struct flatten_agg_col *c = data->agg_output_cols; c; c = c->next)
    zsv_writer_cell(data->csv_writer, !i++, c->column.name, c->column.name_len, 1);
  data->output_row = 1;
}

static unsigned char *flatten_replace_delim(unsigned char *inout,
                                            const unsigned char *delimiter, char replacement) {
  if(!inout)
    return NULL;

  if(!strstr((char *)inout, (char *)delimiter))
    return inout;

  unsigned int delim_len = strlen((char *)delimiter);
  unsigned int j = strlen((char *)inout);
  unsigned char *new_s = malloc(j + 1);
  int new_s_len = 0;
  char clen;
  for(unsigned int i = 0; i < j; i += clen) {
    clen = ZSV_UTF8_CHARLEN_NOERR((int)inout[i]);
    if(i + clen <= j && strncmp((char *)inout + i, (char *)delimiter, delim_len))
      for(int k = 0; k < clen; k++)
        new_s[new_s_len++] = inout[i + k];
    else
      new_s[new_s_len++] = replacement;
  }
  if(new_s)
    new_s[new_s_len++] = 0;
  free(inout);
  return new_s;
}

static void output_current_row(struct flatten_data *data) {
  if(data->last_asset_id) {
    data->output_row++;
    zsv_writer_cell(data->csv_writer, 1, data->last_asset_id,
                      data->last_asset_id_len, 1);
    for(struct flatten_output_column *col = data->output_columns_by_value_head;
        col; col = col->next) {
      zsv_writer_cell(data->csv_writer, 0, col->current_value,
                        col->current_value ? strlen((char *)col->current_value) : 0,
                        1);
    }

    for(struct flatten_agg_col *c = data->agg_output_cols; c; c = c->next) {
      const unsigned char *delimiter = flatten_agg_col_delimiter(c);
      if(!delimiter)
        delimiter = (const unsigned char *)"";
      size_t delimiter_len = strlen((const char *)delimiter);
      const char replacement = (*delimiter == '_' ? '.' : '_');

      // first, calc the length of joined string that we will need to create
      size_t joined_len = 0;

      struct flatten_agg_col_iterator it;
      int i = 0;
      for(flatten_agg_col_iterator_init(c, &it); !flatten_agg_col_iterator_done(&it);
          flatten_agg_col_iterator_next(&it), i++) {
        if(i)
          joined_len += delimiter_len;
        it.str = flatten_replace_delim(it.str, delimiter, replacement);
        flatten_agg_col_iterator_replace_str(&it, &it.str);
        if(it.str && *it.str)
          joined_len += strlen((char *)it.str);
      }

      unsigned char *value_to_print;
      size_t length_to_print;
      if(!joined_len || !(value_to_print = malloc(joined_len))) {
        value_to_print = NULL;
        length_to_print = 0;
      } else {
        unsigned char *cursor = value_to_print;
        length_to_print = joined_len;

        i = 0;
        for(flatten_agg_col_iterator_init(c, &it); !flatten_agg_col_iterator_done(&it);
            flatten_agg_col_iterator_next(&it), i++) {
          // append delimiter
          if(i) {
            memcpy(cursor, delimiter, delimiter_len);
            cursor += delimiter_len;
          }

          // append value
          if(it.str && *it.str) {
            size_t len = strlen((char *)it.str);
            memcpy(cursor, it.str, len);
            cursor += len;
          }
        }
      }
      zsv_writer_cell(data->csv_writer, 0, value_to_print, length_to_print, 1);
      FREEIF(value_to_print);
      chars_lists_delete(&c->values);
      c->last_value = NULL;
    }
  }

  for(struct flatten_output_column *col = data->output_columns_by_value_head; col; col = col->next)
    FREEIF(col->current_value);
  FREEIF(data->last_asset_id);
}

static void flatten_row2(void *hook) {
  struct flatten_data *data = hook;
  if(data->row_count2 == 0) {
    if(!data->row_id_column.ix_plus_1)
      fprintf(stderr, "No ID column found");
    if(data->current_column_index) {
      // set up the agg column vector
      data->agg_output_cols_vector_size = data->current_column_index;
      data->agg_output_cols_vector = calloc(data->agg_output_cols_vector_size, sizeof(*data->agg_output_cols_vector));
      for(struct flatten_agg_col *c = data->agg_output_cols; c; c = c->next) {
        if(c->column.ix_plus_1)
          data->agg_output_cols_vector[c->column.ix_plus_1-1] = c;
      }
    }
  } else {
    if(!data->current_asset_id && !data->last_asset_id)
      fprintf(stderr, "Warning: disregarding row %i: no asset id", data->row_count2);
    else {
      if(data->last_asset_id && data->last_asset_id != data->current_asset_id) {
        output_current_row(data);
        data->last_asset_id = data->current_asset_id;
        data->last_asset_id_len = data->current_asset_id_len;
      }
      if(data->current_column_name_column && data->current_column_name_value) {
        if(data->current_column_name_column->current_value) {
          fprintf(stderr, "Warning: multiple values for column %s, id %s: %s and %s",
                  data->current_column_name_column->name, data->last_asset_id,
                  data->current_column_name_column->current_value, data->current_column_name_value);
          FREEIF(data->current_column_name_column->current_value);
        }
        data->current_column_name_column->current_value = data->current_column_name_value;
        data->current_column_name_value = NULL;
      }
    }
    data->current_column_name_column = NULL;
    FREEIF(data->current_column_name_value);
  }
  data->current_column_index = 0;
  data->row_count2++;
}

const char *flatten_usage_msg[] =
  {
   APPNAME ": flatten a table, based on a single-column key assuming that rows to flatten always appear in contiguous blocks",
   "",
   "Usage: " APPNAME " [<filename>] [<options>] -- [aggregate_output_spec ...]",
   "Each aggregate output specification is either (i) a single-column aggregation or (future: (ii) the \"*\" placeholder (in conjunction with -a)).",
   "A single-column aggregation consists of the column name or index, followed by the equal sign (=) and then an aggregation method.",
   "If the equal sign should be part of the column name, it can be escaped with a preceding backslash.",
   "",
   "The following aggregation methods may be used:"
//   "  max",
//   "  min",
   "  array (pipe-delimited)",
//   "  arrayjs (json)",
   "  array_<delim> (user-specified delimiter)",
//   "  unique (pipe-delimited)",
//   "  uniquejs (json)",
//   "  unique_<delim> (user-specified delimiter)",
   "",
   "Options:",
   "  -b: output with BOM",
   "  -v, --verbose: display verbose messages",
   "  -C <max columns to output>: maximum number of columns to output",
   "  -m <max rows per aggregation>: defaults to 1024. If this limit is reached for any aggregation,",
   "     an error will be output",
   "  --row-id <Row ID column name>: Required. name of column to group by",
   "  --col-name <Column ID column name>: name of column specifying the output column name",
   "  -V <Value column name>: name of column specifying the output value",
   "  (future: -a <Aggregation method>: aggregation method to use for the select-all placeholder)",
   "  -o <output filename>: name of file to save output to",
   NULL
   /*
     EXAMPLE
     echo 'row,col,val
> A,ltv,100
> A,loanid,A
> A,hi,there
> B,loanid,B
> B,ltv,90
> B,hi,you
> B,xxx,zzz' | zsv flatten --row-id row --col-name col -V val
row,ltv,loanid,hi,xxx
A,100,A,there,
B,90,B,you,zzz
   */
};

static void flatten_usage() {
  for(int i = 0; flatten_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", flatten_usage_msg[i]);
}

void flatten_agg_cols_delete(struct flatten_agg_col **p) {
  if(p && *p) {
    struct flatten_agg_col *next;
    for(struct flatten_agg_col *e = *p; e; e = next) {
      next = e->next;
      FREEIF(e->column.name);
      chars_lists_delete(&e->values);
      free(e);
    }
    *p = NULL;
  }
}

static struct flatten_agg_col *flatten_agg_col_new(const char *arg, int *err) {
  struct flatten_agg_col *e = calloc(1, sizeof(*e));
  if((e->column.name = (unsigned char *)strdup(arg))) {
    e->column.name_len = strlen(arg);
  }

  unsigned char *write = e->column.name;
  unsigned char *write_end = e->column.name + e->column.name_len;
  unsigned char *read = e->column.name;

  unsigned char *agg_method_s = NULL;

  while(read && *read) {
    if(*read == '=') { // end of name!
      *read = '\0';
      agg_method_s = read + 1;
      e->column.name_len = read - e->column.name;
      break;
    } else if(*read == '\\') {
      read++;
      if(!*read)
        break;
    }

    *write = *read;
    write++;
    read++;
  }

  if(agg_method_s) {
    if(!strcmp((const char *)agg_method_s, "array"))
      e->agg_method = flatten_agg_method_array;
    else if(!strncmp((const char *)agg_method_s, "array_", strlen("array_")) &&
            strlen((const char *)agg_method_s) > strlen("array_")) {
      e->agg_method = flatten_agg_method_array;
      e->delimiter = agg_method_s + strlen("array_");
    } else
      *err = zsv_printerr(1, "Unrecognized aggregation method (expected array or array_<delim>): %s",
                      agg_method_s);
  } else {
    *err = zsv_printerr(1, "No aggregation method specified for %s", arg);
    while(write < write_end) {
      *write = '\0';
      write++;
    }
  }
  if(!e->agg_method) {
    *err = 1;
    flatten_agg_cols_delete(&e);
  }

  return e;
}

static void flatten_cleanup(struct flatten_data *data) {
  flatten_agg_cols_delete(&data->agg_output_cols);

  if(data->in && data->in != stdin)
    fclose(data->in);

  if(data->out && data->out != stdout)
    fclose(data->out);

  struct flatten_column_name_and_ix *cnxlist[] =
    {
     &data->row_id_column,
     &data->column_name_column,
     &data->value_column
    };
  for(int i = 0; i < 3; i++) {
    struct flatten_column_name_and_ix *cnx = cnxlist[i];
    if(cnx->free_name)
      free(cnx->name);
  }

  for(struct flatten_output_column *next, *e = data->output_columns_by_value_head; e; e = next) {
    next = e->next;
    flatten_output_column_free(e);
    free(e);
  }

  FREEIF(data->agg_output_cols_vector);
  zsv_writer_delete(data->csv_writer);
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    flatten_usage();
    return 0;
  }

  struct flatten_data data = { 0 };
  struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();

  data.output_columns_by_value_tail = &data.output_columns_by_value_head;
  data.max_rows_per_aggregation = 1024;
  data.max_cols = 1024;

  int err = 0;
  int agg_arg_i = 0;

  for(int arg_i = 1; arg_i < argc; arg_i++) {
    if(!strcmp(argv[arg_i], "--")) {
      agg_arg_i = arg_i + 1;
      break;
    } else if(!strcmp(argv[arg_i], "-b"))
      writer_opts.with_bom = 1;
    else if(!strcmp(argv[arg_i], "-C")) {
      if(!(arg_i + 1 < argc && atoi(argv[arg_i+1]) > 9))
        err = zsv_printerr(1, "%s invalid: should be positive integer > 9 (got %s)", argv[arg_i], argv[arg_i+1]);
      else
        data.max_cols = atoi(argv[++arg_i]);
    } else if(!strcmp(argv[arg_i], "-m")) {
      if(!(arg_i + 1 < argc && atoi(argv[arg_i+1]) > 1))
        err = zsv_printerr(1, "%s invalid: should be positive integer > 1 (got %s)", argv[arg_i], argv[arg_i+1]);
      else
        data.max_rows_per_aggregation = atoi(argv[++arg_i]);
    } else if(!strcmp(argv[arg_i], "--row-id")) { // used to be -i
      if(!(arg_i + 1 < argc && *argv[arg_i + 1]))
        err = zsv_printerr(1, "%s option: missing column name", argv[arg_i]);
      else {
        data.row_id_column.name = (unsigned char *)argv[++arg_i];
        data.row_id_column.name_len = strlen((char *)data.row_id_column.name);
      }
    } else if(!strcmp(argv[arg_i], "--col-name")) { // used to be -c
      if(!(arg_i + 1 < argc && *argv[arg_i + 1]))
        err = zsv_printerr(1, "%s option: missing column name", argv[arg_i]);
      else {
        data.column_name_column.name = (unsigned char *)argv[++arg_i];
        data.column_name_column.name_len = strlen((char *)data.column_name_column.name);
      }
    } else if(!strcmp(argv[arg_i], "-V")) {
      if(!(arg_i + 1 < argc))
        err = zsv_printerr(1, "-V option: missing column name");
      else {
        data.value_column.name = (unsigned char *)argv[++arg_i];
        data.value_column.name_len = strlen((char *)data.value_column.name);
      }
    } else if(!strcmp(argv[arg_i], "-o")) {
      if(!(arg_i + 1 < argc))
        err = zsv_printerr(1, "-o option: missing filename");
      else if(*argv[arg_i+1] == '-')
        err = zsv_printerr(1, "-o option: filename may not start with '-' (got %s)", argv[arg_i+1]);
      else
        data.output_filename = argv[++arg_i];
    } else if(data.in)
      err = zsv_printerr(1, "Input file was specified, cannot also read: %s", argv[arg_i]);
    else if(!(data.in = fopen(argv[arg_i], "rb")))
      err = zsv_printerr(1, "Could not open for reading: %s", argv[arg_i]);
    else
      data.input_path = argv[arg_i];
  }

  if(!data.in) {
#ifdef NO_STDIN
    err = zsv_printerr(1, "Please specify an input file");
#else
    data.in = stdin;
#endif
  }

  if(err) {
    flatten_cleanup(&data);
    return 1;
  }

  if(agg_arg_i && agg_arg_i < argc) {
    struct flatten_agg_col **nextp = &data.agg_output_cols;
    for(int arg_i = 0; !err && arg_i + agg_arg_i < argc; arg_i++) {
      const char *arg = argv[arg_i + agg_arg_i];
      struct flatten_agg_col *cs = flatten_agg_col_new(arg, &err);
      if(cs) {
        data.have_agg = 1;
        *nextp = cs;
        nextp = &cs->next;
      }
    }
  }

  if(!(data.out = data.output_filename ? fopen(data.output_filename, "wb") : stdout))
    err = zsv_printerr(1, "Unable to open %s for writing", data.output_filename);

  int passes = data.column_name_column.name || !data.have_agg ? 2 : 1;
  const char *input_path = NULL;
  FILE *in = NULL;
  char *tmp_fn = NULL;
  zsv_handle_ctrl_c_signal();
  if(passes == 1)
    in = data.in;
  else {
    tmp_fn = zsv_get_temp_filename("zsv_flatten_XXXXXXXX");
    if(tmp_fn) {
      FILE *tmp_f = fopen(tmp_fn, "w+b");
      opts->cell_handler = flatten_cell1;
      opts->row_handler = flatten_row1;
      opts->stream = data.in;
      input_path = data.input_path;
      opts->ctx = &data;

      zsv_parser handle;
      if(zsv_new_with_properties(opts, custom_prop_handler, input_path, opts_used, &handle) != zsv_status_ok)
        err = data.cancelled = zsv_printerr(1, "Unable to create csv parser");
      else {
        zsv_set_scan_filter(handle, zsv_filter_write, tmp_f);
        enum zsv_status status;
        while(!data.cancelled && !zsv_signal_interrupted
              && (status = zsv_parse_more(handle)) == zsv_status_ok)
          ;
        zsv_finish(handle);
        zsv_delete(handle);
        fflush(tmp_f);
        rewind(tmp_f);
      }
      in = tmp_f;
    }
  }

  if(!err) {
    struct zsv_opts opts2 = { 0 };
    opts2.cell_handler = flatten_cell2;
    opts2.row_handler = flatten_row2;
    opts2.ctx = &data;
    data.current_column_index = 0;

    if(!(data.csv_writer = zsv_writer_new(&writer_opts)))
      err = data.cancelled = zsv_printerr(1, "Unable to create csv writer");

    flatten_output_header(&data);

    opts2.stream = in;
    zsv_parser parser = zsv_new(&opts2);
    if(!parser)
      err = data.cancelled = zsv_printerr(1, "Unable to create csv parser");

    enum zsv_status status;
    while(!data.cancelled && !zsv_signal_interrupted
          && (status = zsv_parse_more(parser)) == zsv_status_ok)
      ;
    zsv_finish(parser);
    zsv_delete(parser);
    output_current_row(&data);
  }
  flatten_cleanup(&data);

  if(in && in != stdin)
    fclose(in);

  if(tmp_fn) {
    unlink(tmp_fn);
    free(tmp_fn);
  }

  return err;
}
