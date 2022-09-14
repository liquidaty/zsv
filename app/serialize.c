/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/signal.h>
#include <zsv/utils/arg.h>
#include <stdio.h>
#include <stdlib.h>

#define _GNU_SOURCE 1
#include <string.h>
#include <ctype.h>

#ifndef HAVE_MEMMEM
# include <zsv/utils/memmem.h>
#endif

#ifndef STRING_LIB_INCLUDE
#include <zsv/utils/string.h>
#else
#include STRING_LIB_INCLUDE
#endif

struct serialize_header_name {
  struct serialize_header_name *next;
  unsigned char *str;
};

struct output_header_name {
  unsigned char *str;
  unsigned char *output_str;
  size_t output_len;
};

struct serialize_data {
  FILE *in;
  const char *input_path;
  zsv_csv_writer csv_writer;
  zsv_parser parser;

  unsigned char *row_id;
  char row_id_quoted;

  unsigned int current_row_index;
  unsigned int current_col_index;
  struct serialize_header_name *temp_header_names;

  struct output_header_name *header_names;
  unsigned int col_count;
  size_t output_row_count;

  char *err_msg;
  struct {
    const char *value;
    unsigned char *value_lc; // only used if case_insensitive is set
    size_t len;
    unsigned char case_insensitive:1;
    unsigned char entire:1;
    unsigned dummy:6;
  } filter;
};

struct output_header_name *get_output_header_name(struct serialize_data *data,
                                                  unsigned i) {
  if(i < data->col_count && data->header_names)
    return &data->header_names[i];
  return NULL;
}

static void serialize_cell(void *hook, unsigned char *utf8_value, size_t len) {
  struct serialize_data *data = hook;
  if(data->err_msg)
    return;

  char quoted = zsv_quoted(data->parser);

  if(data->current_row_index == 0) {
    struct serialize_header_name *h = calloc(1, sizeof(*h));
    if(!h)
      asprintf(&data->err_msg, "Out of memory!");
    else {
      if((h->str = malloc(1 + len))) {
        memcpy(h->str, utf8_value, len);
        h->str[len] = '\0';
      }
      h->next = data->temp_header_names;
      data->temp_header_names = h;
    }

    if(data->current_col_index == 0) {
      // write header
      zsv_writer_cell(data->csv_writer, 1,
                        utf8_value, len, quoted);
      zsv_writer_cell(data->csv_writer, 0,
                        (const unsigned char *)"Column", strlen("Column"), 0);
      zsv_writer_cell(data->csv_writer, 0,
                        (const unsigned char *)"Value", strlen("Value"), 0);
    }
  } else if(data->current_col_index == 0) {
    if(data->row_id)
      free(data->row_id);
    if((data->row_id = malloc(1 + len))) {
      memcpy(data->row_id, utf8_value, len);
      data->row_id[len] = '\0';
    }
    data->row_id_quoted = quoted;
  } else if(data->current_col_index < data->col_count) {
    char skip = 0;
    if(data->filter.value) {
      if(data->filter.case_insensitive) {
        if(data->filter.entire) { // case-insensitive, exact / entire-cell. skip if not equal
          int err = 0;
          skip = zsv_strincmp(utf8_value, len, (const unsigned char *)data->filter.value, data->filter.len);
          if(err) {
            skip = 1;
            fprintf(stderr, "Ignoring invalid utf8: %.*s\n", (int)len, utf8_value);
          }
        } else { // case-insensitive, not-entire-cell. skip if not contains
          if(data->filter.value_lc) {
            unsigned char *tmp = zsv_strtolowercase(utf8_value, &len);
            if(tmp) {
              skip = !zsv_strstr(tmp, data->filter.value_lc);
              free(tmp);
            }
          }
        }
      } else {
        if(data->filter.entire) // case-sensitive, exact / entire-cell. skip if not equal
          skip = !(len == data->filter.len && !memcmp(utf8_value, data->filter.value, len));
        else // case-sensitive, not-entire-cell. skip if not contains
          skip = !memmem(utf8_value, len, data->filter.value, data->filter.len);
      }
    }

    if(!skip) {
      // write tuple
      struct output_header_name *header_name =
        get_output_header_name(data, data->current_col_index);

      // write row ID
      zsv_writer_cell(data->csv_writer, 1, data->row_id,
                        strlen((char *)data->row_id), data->row_id_quoted);

      // write column name
      zsv_writer_cell(data->csv_writer, 0,
                        header_name->output_str, header_name->output_len,
                        0);

      // write cell value
      zsv_writer_cell(data->csv_writer, 0, utf8_value, len, quoted);
    }
  }
  data->current_col_index++;
}

static void serialize_overflow(void *hook, unsigned char *utf8_value, size_t len) {
  (void)(hook);
  struct serialize_data *data = hook;
  if(data->err_msg)
    return;
  fprintf(stderr, "overflow! %.*s\n", (int)len, utf8_value);
}

static void serialize_row(void *hook) {
  struct serialize_data *data = hook;
  if(data->err_msg)
    return;

  if(data->current_row_index == 0) {
    if(!data->current_col_index)
      asprintf(&data->err_msg, "No columns read in first row; aborting\n");
    else {
      data->col_count = data->current_col_index;
      data->header_names = calloc(data->col_count, sizeof(*data->header_names));

      unsigned int i = 0, j = data->col_count;
      for(struct serialize_header_name *hn = data->temp_header_names; hn; hn = hn->next, i++, j--) {
        if((data->header_names[j-1].str = hn->str)) {
          if((data->header_names[j-1].output_str =
            zsv_writer_str_to_csv(hn->str, strlen((char *)hn->str))))
            data->header_names[j-1].output_len =
              strlen((char *)data->header_names[j-1].output_str);
        }
        hn->str = NULL;
      }
    }
  }

  if(data->row_id) {
    free(data->row_id);
    data->row_id = NULL;
  }

  data->current_row_index++;
  data->current_col_index = 0;
}

static void serialize_error(void *hook, enum zsv_status status,
                            const unsigned char *err_msg, size_t err_msg_len,
                            unsigned char bad_c, size_t cum_scanned_length) {
  struct serialize_data *data = hook;
  if(data->err_msg)
    return;
  (void)(status);
  (void)(err_msg_len);
  fprintf(stderr, "%s (%c) at %zu\n", err_msg, bad_c, cum_scanned_length);
}

#ifndef APPNAME
#define APPNAME "serialize"
#endif

const char *serialize_usage_msg[] =
  {
   APPNAME ": Serialize a CSV file into Row/Colname/Value triplets",
   "",
   "Usage: " APPNAME " [<filename>]",
   "Serializes a CSV file",
   "",
   "Options:",
   "  -b: output with BOM",
   "  -f <value>, --filter <value>: only output cells with text that contains the given value",
   "  -i, --case-insensitive: use case-insensitive match for the filter value",
   "  -e, --entire: match the entire cell's content",
   NULL
  };

static int serialize_usage() {
  for(int i = 0; serialize_usage_msg[i]; i++)
    fprintf(stderr, "%s\n", serialize_usage_msg[i]);
  return 1;
}

static void serialize_cleanup(struct serialize_data *data) {
  zsv_writer_delete(data->csv_writer);

  free(data->filter.value_lc);
  for(struct serialize_header_name *next, *hn = data->temp_header_names; hn; hn = next) {
    next = hn->next;
    free(hn->str);
    free(hn);
  }

  if(data->header_names) {
    for(unsigned int i = 0; i < data->col_count; i++) {
      free(data->header_names[i].str);
      free(data->header_names[i].output_str);
    }
    free(data->header_names);
  }

  free(data->err_msg);
  free(data->row_id);

  if(data->in && data->in != stdin)
    fclose(data->in);
}

#ifndef MAIN
#define MAIN main
#endif

int MAIN(int argc, const char *argv[]) {
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    serialize_usage();
    return 0;
  } else {
    //    struct zsv_opts opts = zsv_get_default_opts();
    struct zsv_opts opts;
    char opts_used[ZSV_OPTS_SIZE_MAX];
    enum zsv_status stat = zsv_args_to_opts(argc, argv, &argc, argv, &opts, opts_used);
    if(stat != zsv_status_ok)
      return stat;

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
      } else if(!strcmp(arg, "-i") || !strcmp(arg, "--case-insensitive"))
        data.filter.case_insensitive = 1;
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

    opts.cell = serialize_cell;
    opts.row = serialize_row;
    opts.overflow = serialize_overflow;
    opts.error = serialize_error;
    opts.stream = data.in;
    const char *input_path = data.input_path;

    opts.ctx = &data;
    // data.parser = zsv_new(&opts);
    data.csv_writer = zsv_writer_new(&writer_opts);
    if(zsv_new_with_properties(&opts, input_path, opts_used, &data.parser) != zsv_status_ok
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
