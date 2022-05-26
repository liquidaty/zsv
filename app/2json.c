/*
 * Copyright (C) 2021-2022 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
#include <zsv/utils/writer.h>
#include <zsv/utils/signal.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/mem.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <jsonwriter.h>

struct zsv_2json_header {
  struct zsv_2json_header *next;
  char *name;
};

#define LQ_2JSON_MAX_INDEXES 32

struct zsv_2json_data {
  zsv_parser parser;
  jsonwriter_handle jsw;

  size_t rows_processed; // includes header row

  struct {
    const char *clauses[LQ_2JSON_MAX_INDEXES];
    char unique[LQ_2JSON_MAX_INDEXES];
    unsigned count;
  } indexes;

  struct zsv_2json_header *headers, *current_header;
  struct zsv_2json_header **headers_next;

  unsigned char overflowed:1;
#define ZSV_JSON_SCHEMA_OBJECT 1
#define ZSV_JSON_SCHEMA_DATABASE 2
  unsigned char schema:2;
  unsigned char no_header:1;
  unsigned char no_empty:1;
  unsigned char err:1;
  unsigned char _:2;
};

static void zsv_2json_cleanup(struct zsv_2json_data *data) {
  for(struct zsv_2json_header *next, *h = data->headers; h; h = next) {
    next = h->next;
    if(h->name)
      free(h->name);
    free(h);
  }
}

static void write_header_cell(struct zsv_2json_data *data, const unsigned char *utf8_value, size_t len) {
  if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
    struct zsv_2json_header *h;
    if(!(h = calloc(1, sizeof(*h)))) {
      fprintf(stderr, "Out of memory!\n");
      data->err = 1;
    } else {
      *data->headers_next = h;
      data->headers_next = &h->next;
      if((h->name = malloc(len + 1))) {
        memcpy(h->name, utf8_value, len);
        h->name[len] = '\0';
      }
    }
  } else {
    // to do: add options to set data type, etc
    jsonwriter_start_object(data->jsw);
    jsonwriter_object_key(data->jsw, "name");
    jsonwriter_strn(data->jsw, utf8_value, len);
    jsonwriter_end(data->jsw);
  }
}

static void write_data_cell(struct zsv_2json_data *data, const unsigned char *utf8_value, size_t len) {
  if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
    if(!data->current_header)
      return;
    char *current_header_name = data->current_header->name;
    data->current_header = data->current_header->next;

    if(len || !data->no_empty)
      jsonwriter_object_key(data->jsw, current_header_name);
    else
      return;
  }
  jsonwriter_strn(data->jsw, utf8_value, len);
}

void zsv_2json_overflow(void *ctx, unsigned char *utf8_value, size_t len) {
  struct zsv_2json_data *data = ctx;
  if(len) {
    if(!data->overflowed) {
      fwrite("overflow! ", 1, strlen("overflow! "), stderr);
      fwrite(utf8_value, 1 ,len, stderr);
      fprintf(stderr, "(subsequent overflows will be suppressed)");
      data->overflowed = 1;
    }
  }
}

static void zsv_2json_row(void *ctx) {
  struct zsv_2json_data *data = ctx;
  unsigned int cols = zsv_column_count(data->parser);
  if(cols) {
    char obj = 0;
    char arr = 0;
    if(!data->rows_processed) { // header row
      jsonwriter_start_array(data->jsw); // start array of rows
      if(data->schema == ZSV_JSON_SCHEMA_DATABASE) {
        jsonwriter_start_object(data->jsw); // start this row
        obj = 1;

        // to do: check index syntax (as of now, we just take argument value
        // as-is and assume it will translate into a valid SQLITE3 command)
        char have_index = 0;
        for(unsigned i = 0; i < data->indexes.count; i++) {
          const char *name_start = data->indexes.clauses[i];
          const char *on = strstr(name_start, " on ");
          if(on) {
            on += 4;
            while(*on == ' ')
              on++;
          }
          if(!on || !*on)
            continue;

          const char *name_end = name_start;
          while(name_end && *name_end && *name_end != ' ')
            name_end++;
          if(name_end > name_start) {
            if(!have_index) {
              have_index = 1;
              jsonwriter_object_object(data->jsw, "indexes");
            }
            char *tmp = zsv_memdup(name_start, name_end - name_start);
            jsonwriter_object_object(data->jsw, tmp); // this index
            free(tmp);
            jsonwriter_object_cstr(data->jsw, "on", on);
            if(data->indexes.unique[i])
              jsonwriter_object_bool(data->jsw, "unique", 1);
            jsonwriter_end_object(data->jsw); // end this index
          }
        }
        if(have_index)
          jsonwriter_end_object(data->jsw); // indexes

        jsonwriter_object_array(data->jsw, "columns");
        arr = 1;
      } else if(data->schema != ZSV_JSON_SCHEMA_OBJECT) {
        jsonwriter_start_array(data->jsw); // start this row
        arr = 1;
      }
    } else { // processing a data row, not header row
      if(data->schema == ZSV_JSON_SCHEMA_OBJECT) {
        jsonwriter_start_object(data->jsw); // start this row
        obj = 1;
      } else {
        if(data->rows_processed == 1 && data->schema == ZSV_JSON_SCHEMA_DATABASE)
          jsonwriter_start_array(data->jsw); // start the table-data element
        jsonwriter_start_array(data->jsw); // start this row
        arr = 1;
      }
    }

    for(unsigned int i = 0; i < cols; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      // output this cell
      if(!data->rows_processed && !data->no_header) // this is header row
        write_header_cell(data, cell.str, cell.len);
      else
        write_data_cell(data, cell.str, cell.len);
    }

    // end this row
    if(arr)
      jsonwriter_end_array(data->jsw);
    if(obj)
      jsonwriter_end_object(data->jsw);
    data->rows_processed++;
  }
  data->current_header = data->headers;
}

#ifndef MAIN
#define MAIN main
#endif

int MAIN(int argc, const char *argv[]) {
  FILE *f_in = NULL;
  struct zsv_2json_data data = { 0 };
  data.headers_next = &data.headers;

  INIT_CMD_DEFAULT_ARGS();

  const char *usage[] =
    {
     "Usage: zsv_2json [input.csv]\n",
     "  Reads CSV input and converts to json",
     "",
     "Options:",
     "  -h, --help",
     "  -o, --output <filename>       : output to specified filename",
     "  --object                      : output as array of objects",
     "  --no-empty                    : omit empty properties (only with --object)",
     "  --database                    : output in database schema",
     "  --no-header                   : treat the header row as a data row",
     "  --index <name on expr>        : add index to database schema",
     "  --unique-index <name on expr> : add unique index to database schema",
     NULL
    };

  FILE *out = NULL;
  int err = 0;
  struct zsv_opts opts = zsv_get_default_opts(); // leave up here so that below goto stmts do not cross initialization
  for(int i = 1; !err && i < argc; i++) {
    if(!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      for(int j = 0; usage[j]; j++)
        fprintf(stderr, "%s\n", usage[j]);
      goto exit_2json;
    } else if(!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = 1;
      else if(out)
        fprintf(stderr, "Output file specified more than once\n"), err = 1;
      else if(!(out = fopen(argv[i], "wb")))
        fprintf(stderr, "Unable to open for writing: %s\n", argv[i]), err = 1;
    } else if(!strcmp(argv[i], "--index") || !strcmp(argv[i], "--unique-index")) {
      if(++i >= argc)
        fprintf(stderr, "%s option requires a filename value\n", argv[i-1]), err = 1;
      else if(data.indexes.count > LQ_2JSON_MAX_INDEXES)
        fprintf(stderr, "Max index count exceeded; ignoring %s\n", argv[i]), err = 1;
      else if(!strstr(argv[i], " on "))
        fprintf(stderr, "Index value should be in the form of 'index_name on expr'; got %s\n", argv[i]), err = 1;
      else {
        if(!strcmp(argv[i-1], "--unique-index"))
          data.indexes.unique[data.indexes.count] = 1;
        data.indexes.clauses[data.indexes.count++] = argv[i];
      }
    } else if(!strcmp(argv[i], "--no-empty")) {
      data.no_empty = 1;
    } else if(!strcmp(argv[i], "--database") || !strcmp(argv[i], "--object")) {
      if(data.schema)
        fprintf(stderr, "Output schema specified more than once\n"), err = 1;
      else if(!strcmp(argv[i], "--database"))
        data.schema = ZSV_JSON_SCHEMA_DATABASE;
      else
        data.schema = ZSV_JSON_SCHEMA_OBJECT;
    } else if(!strcmp(argv[i], "--no-header"))
      data.no_header = 1;
    else {
      if(f_in)
        fprintf(stderr, "Input file specified more than once\n"), err = 1;
      else if(!(f_in = fopen(argv[i], "rb")))
        fprintf(stderr, "Unable to open for reading: %s\n", argv[i]), err = 1;
    }
  }

  if(data.indexes.count && data.schema != ZSV_JSON_SCHEMA_DATABASE)
    fprintf(stderr, "--index/--unique-index can only be used with --database"), err = 1;
  else if(data.no_header && data.schema)
    fprintf(stderr, "--no-header cannot be used together with --object or --database"), err = 1;
  else if(data.no_empty && data.schema != ZSV_JSON_SCHEMA_OBJECT)
    fprintf(stderr, "--no-empty can only be used with --object"), err = 1;
  else if(!f_in) {
#ifdef NO_STDIN
    fprintf(stderr, "Please specify an input file\n"), err = 1;
#else
    f_in = stdin;
#endif
  }

  if(err) {
    if(f_in && f_in != stdin)
      fclose(f_in);
    if(out)
      fclose(out);
    goto exit_2json;
  }

  if(!out)
    out = stdout;

  opts.row = zsv_2json_row;
  opts.ctx = &data;
  opts.overflow = zsv_2json_overflow;

  if((data.jsw = jsonwriter_new(out))) {
    opts.stream = f_in;
    if((data.parser = zsv_new(&opts))) {
      zsv_handle_ctrl_c_signal();
      enum zsv_status status;
      while(!data.err
            && !zsv_signal_interrupted
            && (status = zsv_parse_more(data.parser)) == zsv_status_ok)
        ;
      zsv_finish(data.parser);
      zsv_delete(data.parser);
      jsonwriter_end_all(data.jsw);
    }
    jsonwriter_delete(data.jsw);
  }
  zsv_2json_cleanup(&data);
  if(out && out != stdout)
    fclose(out);
  if(f_in && f_in != stdin)
    fclose(f_in);

  err = data.err;

 exit_2json:
  return err;
}
