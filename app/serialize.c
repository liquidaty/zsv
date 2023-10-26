/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>

#define _GNU_SOURCE 1
#include <string.h>
#include <ctype.h>

#define ZSV_COMMAND serialize
#include "zsv_command.h"

#include <zsv/utils/writer.h>
#include <zsv/utils/string.h>

struct output_header_name {
  unsigned char *str;
  size_t len;
};

struct serialize_additional_column {
  struct serialize_additional_column *next;
  unsigned char *name;
  size_t len;
  size_t position_plus_1; // 0 = unassigned / not found
};

struct serialize_data {
  FILE *in;
  const char *input_path;
  zsv_csv_writer csv_writer;
  zsv_parser parser;

  struct output_header_name *header_names;
  unsigned int col_count;
  unsigned int id_column_position;

  char *err_msg;

  struct serialize_additional_column *additional_columns;
  struct serialize_additional_column **additional_columns_next;
  struct {
    const char *value;
    unsigned char *value_lc; // only used if case_insensitive is set
    size_t len;
    unsigned char case_insensitive:1;
    unsigned char entire:1;
    unsigned _:6;
  } filter;
  unsigned char use_column_position:1;
  unsigned char _:7;
};

static void serialize_write_tuple(struct serialize_data *data,
                                  const unsigned char *id, size_t id_len, char id_quoted,
                                  const unsigned char *colname, size_t colname_len,
                                  const unsigned char *s, size_t len, char quoted) {
  // write row ID, column name, cell value
  zsv_writer_cell(data->csv_writer, 1, id, id_len, id_quoted);
  zsv_writer_cell(data->csv_writer, 0, colname, colname_len, 0);
  zsv_writer_cell(data->csv_writer, 0, s, len, quoted);

  // to do: write additional column headers
  for(struct serialize_additional_column *col = data->additional_columns; col; col = col->next) {
    if(col->position_plus_1) {
      struct zsv_cell c = zsv_get_cell(data->parser, col->position_plus_1 - 1);
      zsv_writer_cell(data->csv_writer, 0, c.str, c.len, c.quoted);
    }
  }
}

static inline void serialize_cell(struct serialize_data *data,
                                  struct zsv_cell id, unsigned i) {
  struct zsv_cell cell = zsv_get_cell(data->parser, i);
  char skip = 0;
  if(data->filter.value) {
    if(data->filter.case_insensitive) {
      if(data->filter.entire) {
        int err = 0;
        skip = zsv_strincmp(cell.str, cell.len, (const unsigned char *)data->filter.value, data->filter.len);
        if(err) {
          skip = 1;
          fprintf(stderr, "Ignoring invalid utf8: %.*s\n", (int)cell.len, cell.str);
        }
      } else { // case-insensitive, not-entire-cell. skip if not contains
        if(data->filter.value_lc) {
          size_t len = cell.len;
          unsigned char *tmp = zsv_strtolowercase(cell.str, &len);
          if(tmp) {
            skip = !zsv_strstr(tmp, data->filter.value_lc);
            free(tmp);
          }
        }
      }
    } else {
      if(data->filter.entire) // case-sensitive, exact / entire-cell. skip if not equal
        skip = !(cell.len == data->filter.len && !memcmp(cell.str, data->filter.value, cell.len));
      else // case-sensitive, not-entire-cell. skip if not contains
        skip = !memmem(cell.str, cell.len, data->filter.value, data->filter.len);
    }
  }

  if(!skip)
    // write tuple
    serialize_write_tuple(data, id.str, id.len, id.quoted,
                          data->header_names[i].str, data->header_names[i].len,
                          cell.str, cell.len, cell.quoted);
}

static void serialize_row(void *hook);

static void serialize_header(void *hook) {
  struct serialize_data *data = hook;
  zsv_set_row_handler(data->parser, serialize_row);
  if(data->err_msg)
    return;

  data->col_count = zsv_cell_count(data->parser);
  if(!data->col_count)
    asprintf(&data->err_msg, "No columns read in first row; aborting\n");
  else {
    // write header
    struct zsv_cell idCell = zsv_get_cell(data->parser, data->id_column_position);
    if(idCell.len == 0) {
      idCell.str = (unsigned char *)"(Blank)";
      idCell.len = strlen((const char *)idCell.str);
    }
    // if we have additional columns, find them and output their header names
    if(data->additional_columns) {
      for(struct serialize_additional_column *c = data->additional_columns;
          c && !data->err_msg; c = c->next) {
        for(unsigned i = 0; i < data->col_count && !data->err_msg; i++) {
          struct zsv_cell tmp = zsv_get_cell(data->parser, i);

          if(!zsv_strincmp(tmp.str, tmp.len, c->name, c->len)) {
            // found
            c->position_plus_1 = i + 1;
            break;
          }
        }
        if(!c->position_plus_1)
          asprintf(&data->err_msg, "Column '%s' not found\n", (char *)c->name);
      }
    }
    if(!data->err_msg) {
      data->header_names = calloc(data->col_count, sizeof(*data->header_names));
      if(!data->header_names)
        asprintf(&data->err_msg, "Out of memory!");
    }

    for(unsigned i = 1; i < data->col_count && !data->err_msg; i++) {
      struct zsv_cell cell  = zsv_get_cell(data->parser, i);
      // save the column header name
      if(data->use_column_position)
        asprintf((char **)&data->header_names[i].str, "%u", i);
      else {
        struct zsv_cell c = zsv_get_cell(data->parser, i);
        if(i == 0 && c.len == 0)
          data->header_names[i].str = (unsigned char *)strdup("(Blank)");
        else
          data->header_names[i].str = zsv_writer_str_to_csv(cell.str, cell.len);
      }

      if(data->header_names[i].str)
        data->header_names[i].len = strlen((const char *)data->header_names[i].str);
      else if(cell.len)
        asprintf(&data->err_msg, "Out of memory!");
    }

    if(!data->err_msg) {
      // print the output table header
      serialize_write_tuple(data, idCell.str, idCell.len, idCell.quoted,
                            (const unsigned char *)"Column", strlen("Column"),
                            (const unsigned char *)"Value", strlen("Value"), 0);

      if(data->use_column_position) {
        // process the header row as if it was a data row
        // output ID cell
        struct zsv_cell cell = zsv_get_cell(data->parser, data->id_column_position);
        serialize_write_tuple(data, (const unsigned char *)"Header", strlen("Header"), 0,
                              (const unsigned char *)"0", 1,
                              cell.str, cell.len, cell.quoted);

        // output other cells
        for(unsigned i = 0; i < data->col_count; i++) {
          if(i != data->id_column_position) {
            cell = zsv_get_cell(data->parser, i);
            serialize_write_tuple(data, (const unsigned char *)"Header", strlen("Header"), 0,
                                  data->header_names[i].str, data->header_names[i].len,
                                  cell.str, cell.len, cell.quoted);
          }
        }
      }
    }
  }
}

static void serialize_row(void *hook) {
  struct serialize_data *data = hook;
  if(data->err_msg)
    return;

  // get the row id
  struct zsv_cell id = zsv_get_cell(data->parser, data->id_column_position);

  unsigned j = zsv_cell_count(data->parser);
  for(unsigned i = 0; i < j && i < data->col_count; i++)
    if(i != data->id_column_position)
      serialize_cell(data, id, i);
}

const char *serialize_usage_msg[] =
  {
   APPNAME ": Serialize a CSV file into Row/Colname/Value triplets",
   "",
   "Usage: " APPNAME " [<filename>]",
   "Serializes a CSV file",
   "",
   "Options:",
   "  -b                     : output with BOM",
   "  -f,--filter <value>    : only output cells with text that contains the given value",
   "  -e,--entire            : match the entire cell's content (only applicable with -f)",
   "  -i,--case-insensitive  : use case-insensitive match for the filter value",
   "  --id-column <n>        : the 1-based position of the column to use as the identifer (default=1, max=2000)",
   "  -p,--position          : output column position instead of name; the second column",
   "                           will be position 1, and the first row will be treated as a",
   "                           normal data row",
   "  -a,--add <column name> : add additional columns to output. may be specified",
   "                           multiple times for multiple additional columns",
   NULL
  };

static int serialize_usage() {
  for(int i = 0; serialize_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", serialize_usage_msg[i]);
  return 1;
}

static void serialize_cleanup(struct serialize_data *data) {
  zsv_writer_delete(data->csv_writer);

  free(data->filter.value_lc);
  free(data->err_msg);

  if(data->header_names) {
    for(unsigned int i = 0; i < data->col_count; i++)
      free(data->header_names[i].str);
    free(data->header_names);
  }

  if(data->in && data->in != stdin)
    fclose(data->in);

  for(struct serialize_additional_column *next, *c = data->additional_columns; c; c = next) {
    next = c->next;
    free(c->name);
    free(c);
  }
}

static int serialize_append_additional_column(struct serialize_data *data, const char *name) {
  struct serialize_additional_column *c = calloc(1, sizeof(*c));
  if(c) {
    if((c->name = (unsigned char *)strdup(name))) {
      c->len = strlen((char *)c->name);
      if(!data->additional_columns)
        data->additional_columns = c;
      else
        *data->additional_columns_next = c;
      data->additional_columns_next = &c->next;
      return 0;
    }
    free(c);
  }
  return 1;
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    serialize_usage();
    return 0;
  } else {
    struct zsv_csv_writer_options writer_opts = zsv_writer_get_default_opts();
    struct serialize_data data = { 0 };
    int err = 0;
    for(int arg_i = 1; !err && arg_i < argc; arg_i++) {
      const char *arg = argv[arg_i];
      if(!strcmp(arg, "-f") || !strcmp(arg, "--filter")) {
        if(arg_i + 1 < argc)
          data.filter.value = argv[++arg_i];
        else {
          fprintf(stderr, "filter option requires a value\n");
          err = 1;
        }
      } else if(!strcmp(arg, "-a") || !strcmp(arg, "--add")) {
        if(arg_i + 1 < argc)
          err = serialize_append_additional_column(&data, argv[++arg_i]);
        else {
          fprintf(stderr, "%s option requires a value\n", argv[arg_i]);
          err = 1;
        }
      } else if(!strcmp(arg, "-i") || !strcmp(arg, "--case-insensitive"))
        data.filter.case_insensitive = 1;
      else if(!strcmp(arg, "--id-column")) {
        if(arg_i + 1 < argc && atoi(argv[arg_i+1]) > 0 && atoi(argv[arg_i+1]) <= 2000)
          data.id_column_position = atoi(argv[++arg_i]) - 1;
        else {
          fprintf(stderr, "%s option requires a value between 1 and 2000\n", argv[arg_i]);
          err = 1;
        }
      }
      else if(!strcmp(arg, "-p") || !strcmp(arg, "--position"))
        data.use_column_position = 1;
      else if(!strcmp(arg, "-e") || !strcmp(arg, "--entire"))
        data.filter.entire = 1;
      else if(!strcmp(argv[arg_i], "-b"))
        writer_opts.with_bom = 1;
      else if(data.in) {
        err = 1;
        fprintf(stderr, "Input file specified twice, or unrecognized argument: %s\n", argv[arg_i]);
      } else if(!(data.in = fopen(argv[arg_i], "rb"))) {
        err = 1;
        fprintf(stderr, "Could not open for reading: %s\n", argv[arg_i]);
      } else
        data.input_path = argv[arg_i];
    }

    if(data.filter.value) {
      data.filter.len = strlen(data.filter.value);
      if(data.filter.case_insensitive)
        data.filter.value_lc =
          zsv_strtolowercase((const unsigned char *)data.filter.value,
                               &data.filter.len);
    }

    if(!data.in) {
#ifdef NO_STDIN
      fprintf(stderr, "Please specify an input file\n");
      err = 1;
#else
      data.in = stdin;
#endif
    }

    if(err) {
      serialize_cleanup(&data);
      return 1;
    }

    opts->row_handler = serialize_header;
    opts->stream = data.in;
    const char *input_path = data.input_path;

    opts->ctx = &data;
    data.csv_writer = zsv_writer_new(&writer_opts);
    if(zsv_new_with_properties(opts, custom_prop_handler, input_path, opts_used, &data.parser) != zsv_status_ok
       || !data.csv_writer) {
      serialize_cleanup(&data);
      return 1;
    }

    // create a local csv writer buff for faster performance
    unsigned char writer_buff[64];
    zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

    // process the input data
    zsv_handle_ctrl_c_signal();
    enum zsv_status status;
    while(!zsv_signal_interrupted && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
      ;

    if(data.err_msg)
      fprintf(stderr, "Error: %s\n", data.err_msg);
    zsv_finish(data.parser);
    zsv_delete(data.parser);
    serialize_cleanup(&data);
  }
  return 0;
}
