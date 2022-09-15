/* Copyright (C) 2022 Guarnerix Inc dba Liquidaty - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Matt Wong <matt@guarnerix.com>
 */

/*
  for a given file, edits properties saved in .zsv/data/<filepath>/props.json
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <yajl_helper.h>

#define ZSV_COMMAND_NO_OPTIONS
#define ZSV_COMMAND prop
#include "zsv_command.h"

#include <zsv/utils/os.h>
#include <zsv/utils/json.h>
#include <zsv/utils/cache.h>

const char *zsv_property_usage_msg[] = {
  APPNAME ": save parsing options associated with a file that are subsequently",
  "          applied by default when processing that file",
  "",
  "Usage: " APPNAME " <filepath> <command> <arguments>",
  "  where command can be:",
  "    set <property_id> <value>",
  "    unset <property_id>",
  "    detect [-s,--save [-f,--force]]: try to auto-detect the best skip-head and header-row-span values",
  "    show-all:  show all properties",
  "    unset-all: delete all properties",
  "",
  "  and property_id can be any of:",
  "    skip-head: skip n rows before reading header row(s)",
  "     This option corresponds to -R/--skip-head parsing option",
  "    header-row-span: set header depth (rowspan) (if < 2, rowspan is assumed to be 1)",
  "     This option corresponds to -d/--header-row-span parsing option",
  "",
  "  Properties are saved in " ZSV_CACHE_DIR "/<filename>/" ZSV_CACHE_PROPERTIES_NAME,
  "    which is deleted when the file is removed using `rm`",
  NULL
};

static int zsv_property_usage(FILE *target) {
  for(int j = 0; zsv_property_usage_msg[j]; j++)
    fprintf(target, "%s\n", zsv_property_usage_msg[j]);
  return target == stdout ? 0 : 1;
}

static int show_all_properties(const unsigned char *filepath) {
  int err = zsv_cache_print(filepath, zsv_cache_type_property, (const unsigned char *)"{}");
  if(!err)
    printf("\n");
  return err;
}

#define ZSV_PROP_TYPE_CHECK_NUM 1
#define ZSV_PROP_TYPE_CHECK_DATE 2
#define ZSV_PROP_TYPE_CHECK_BOOL 4
#define ZSV_PROP_TYPE_CHECK_NULL 8

static char zsv_str_is_num(const unsigned char *s, size_t slen, unsigned flags) {
  return 0;
}

static char zsv_str_is_date(const unsigned char *s, size_t slen, unsigned flags) {
  return 0;
}

static char zsv_str_is_bool(const unsigned char *s, size_t slen, unsigned flags) {
  return 0;
}


static unsigned int type_detect(const unsigned char *s, size_t slen) {
  unsigned int result = 0;
  if(slen == 0) {
    result += ZSV_PROP_TYPE_CHECK_NULL;
    return result;
  }
  if(zsv_str_is_num(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_NUM;
  if(zsv_str_is_date(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_DATE;
  if(zsv_str_is_bool(s, slen, 0))
    result += ZSV_PROP_TYPE_CHECK_BOOL;
  return result;
}

struct detect_opts {
  char save;
  char force;
};

#define ZSV_PROP_DETECT_ROW_MAX 10
struct detect_properties_data {
  zsv_parser parser;
  struct {
    size_t date;
    size_t num;
    size_t is_bool;
    size_t null;
    size_t cols_used;
  } rows[ZSV_PROP_DETECT_ROW_MAX];
  size_t rows_processed;
};

static void detect_properties_row(void *ctx) {
  struct detect_properties_data *data = ctx;
  size_t cols_used = data->rows[data->rows_processed].cols_used = zsv_column_count(data->parser);
  for(size_t i = 0; i < cols_used; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    unsigned int result = type_detect(c.str, c.len);
    if(result & ZSV_PROP_TYPE_CHECK_NULL)
      data->rows[data->rows_processed].null++;
    else {
      if(result & ZSV_PROP_TYPE_CHECK_NUM)
        data->rows[data->rows_processed].num++;
      if(result & ZSV_PROP_TYPE_CHECK_DATE)
        data->rows[data->rows_processed].date++;
      if(result & ZSV_PROP_TYPE_CHECK_BOOL)
        data->rows[data->rows_processed].is_bool++;
    }
  }
  if(++data->rows_processed >= ZSV_PROP_DETECT_ROW_MAX)
    zsv_abort(data->parser);
}

static int unset_all_properties(const unsigned char *filepath) {
  return zsv_cache_remove(filepath, zsv_cache_type_property);
}

static int print_zsv_file_properties(struct zsv_file_properties *gp,
                                     const unsigned char *filepath,
                                     struct detect_opts *detect_opts) {
  int err = 0;
  const char *format = "{\n  \"skip-head\": %u,\n  \"header-row-span\": %u\n}%s";
  printf(format, gp->skip, gp->header_span, "\n");
  if(detect_opts->save) {
    unsigned char *props_fn = zsv_cache_filepath(filepath, zsv_cache_type_property, 0, 0);
    if(!props_fn)
      err = 1;
    else {
      if(!detect_opts->force) {
        struct zsv_opts zsv_opts = { 0 };
        struct zsv_file_properties fp = { 0 };
        err = zsv_cache_load_props((const char *)filepath, &zsv_opts, &fp, NULL);
        // err = load_properties(filepath, &zsv_opts, &fp);
        if(!err) {
          if(fp.header_span_specified || fp.skip_specified) {
            fprintf(stderr, "Properties for this file already exist; use -f or --force option to overwrite\n");
            err = 1;
          }
        }
      }
      if(!err) {
        if(!(gp->skip || gp->header_span))
          err = unset_all_properties(filepath);
        else {
          unsigned char *props_fn_tmp = zsv_cache_filepath(filepath, zsv_cache_type_property, 1, 1);
          if(!props_fn_tmp)
            err = 1;
          else {
            // open a temp file, then write, then replace the orig file
            FILE *f = fopen((char *)props_fn_tmp, "wb");
            if(!f) {
              perror((char *)props_fn_tmp);
              err = 1;
            } else {
              if(!fprintf(f, format, gp->skip, gp->header_span, ""))
                err = 1;
              else {
                if(zsv_replace_file(props_fn_tmp, props_fn))
                  err = zsv_printerr(-1, "Unable to save %s", props_fn);
              }
              fclose(f);
            }
          }
          free(props_fn_tmp);
        }
      }
      free(props_fn);
    }
  }
  return err;
}

static struct zsv_file_properties guess_properties(struct detect_properties_data *data) {
  struct zsv_file_properties result = { 0 };
  size_t i;
  for(i = 0; i < data->rows_processed; i++) {
    if(data->rows[i].cols_used <= data->rows[i].null)
      result.skip++;
    else if(i + 1 < data->rows_processed && data->rows[i+1].cols_used <= data->rows[i+1].null)
      result.skip++;
    else
      break;
  }

  result.header_span = 1;
  for(i = i+1; i < data->rows_processed; i++, result.header_span++) {
    if(data->rows[i].cols_used == data->rows[i].null)
      continue;
    if(data->rows[i].cols_used > data->rows[i-1].cols_used)
      continue;
    unsigned int data_like = data->rows[i].date + data->rows[i].num + data->rows[i].is_bool;
    if(data_like > 5 || (data_like > 0 && data_like >= data->rows[i].cols_used / 2))
      break;
    if(data_like + data->rows[i].null == data->rows[i].cols_used)
      break;
    if(data_like <= 5 && data_like > 0 && data->rows[i].cols_used <= 5)
      break;
  }
  return result;
}

static int detect_properties(const unsigned char *filepath, struct detect_opts detect_opts) {
  struct zsv_opts opts;
  struct detect_properties_data data = { 0 };

  opts = zsv_get_default_opts();
  opts.row = detect_properties_row;
  opts.ctx = &data;
  if(!strcmp((void *)filepath, "-"))
    opts.stream = stdin;
  else {
    opts.stream = fopen((const char *)filepath, "rb");
    if(!opts.stream) {
      perror((const char *)filepath);
      return 1;
    }
  }

  opts.keep_empty_header_rows = 1;
  // load_properties_init() will not run because opts
  data.parser = zsv_new(&opts);
  while(!zsv_signal_interrupted && zsv_parse_more(data.parser) == zsv_status_ok)
    ;
  zsv_finish(data.parser);
  zsv_delete(data.parser);

  fclose(opts.stream);
  struct zsv_file_properties result = guess_properties(&data);
  result.header_span += opts.header_span;
  result.skip += opts.rows_to_ignore;
  return print_zsv_file_properties(&result, filepath, &detect_opts);
}

static int set_unset_property(const unsigned char *filepath, const char *cmd,
                               const char *prop_id, const char *integer_str) {
  int err = 0;
  unsigned char *prop_id_json = zsv_json_from_str((void *)prop_id);
  if(!prop_id_json)
    err = zsv_printerr(ENOMEM, "Out of memory!");
  else {
    const unsigned char *filter = (const unsigned char *)
      (!strcmp(cmd, "set") ?
       ".[1] as $k|.[2] as $v|.[0] + ([{key:$k,value:$v}]|from_entries)" :
       ".[1] as $k|(.[0]|to_entries|map(select(.key!=$k))|from_entries)"
       );
    err = zsv_modify_cache_file(filepath, zsv_cache_type_property, prop_id_json, (const unsigned char *)integer_str, filter);
    // to do: debug "jv_parser_free" error given bad json such as:
    //   err = modify_cache_file(filepath, zsv_cache_type_property, prop_id, integer_str, filter);
  }
  free(prop_id_json);
  return err;
}

static char property_id_is_ok(const char *s) {
  return (!strcmp(s, "skip-head") || !strcmp(s, "header-row-span"));
}

int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int m_argc, const char *m_argv[]) {
  int err = 0;
  if(m_argc > 1 && (!strcmp(m_argv[1], "-h") || !strcmp(m_argv[1], "--help")))
    err = zsv_property_usage(stdout);
  else if(m_argc < 3)
    err = zsv_property_usage(stderr);
  else {
    const unsigned char *filepath = (const unsigned char *)m_argv[1];
    const char *cmd = m_argv[2];
    const char *property_id = NULL;
    const char *value = NULL;

    int mode_args = 0;
    enum zsv_property_mode {
      zsv_property_mode_show_all = 1,
      zsv_property_mode_unset_all,
      zsv_property_mode_set_unset,
      zsv_property_mode_detect
    } mode = 0;

    if(!strcmp(cmd, "show-all"))
      mode = zsv_property_mode_show_all;
    else if(!strcmp(cmd, "unset-all"))
      mode = zsv_property_mode_unset_all;
    else if(!strcmp(cmd, "detect"))
      mode = zsv_property_mode_detect;
    else if(!strcmp(cmd, "set") || !strcmp(cmd, "unset")) {
      mode = zsv_property_mode_set_unset;
      mode_args = *cmd == 's' ? 2 : 1;
      if(*cmd == 's' && (m_argc < 5 || !*m_argv[3] || !*m_argv[4]))
        err = zsv_printerr(1, "%s command requires <property_id> and <value>", cmd);
      else if(*cmd == 'u' && (m_argc < 4 || !*m_argv[3]))
        err = zsv_printerr(1, "%s command requires <property_id>", cmd);
      else {
        property_id = m_argv[3];
        if(!property_id_is_ok(property_id))
          err = zsv_printerr(1, "Unrecognized property id; expected 'skip-head' or 'header-row-span', got %s", property_id);
        else {
          value = *cmd == 's' ? m_argv[4] : "null";
          if(*cmd == 's') { // check that value consists solely of digits
            for(const char *s = value; !err && *s; s++) {
              if(!strchr("1234567890", *s)) {
                fprintf(stderr, "value should be an integer >= 0; got %s\n", value);
                err = 1;
              }
            }
          }
        }
      }
    } else
      err = zsv_printerr(1, "Unrecognized command: %s", cmd);

    struct detect_opts detect_opts = { 0 };
    for(int i = 3 + mode_args; !err && i < m_argc; i++) {
      // options go here
      const char *arg = m_argv[i];
      if(mode == zsv_property_mode_detect) {
        if(!strcmp("-s", arg) || !strcmp("--save", arg)) {
          detect_opts.save = 1;
          continue;
        }
        if(!strcmp("-f", arg) || !strcmp("--force", arg)) {
          detect_opts.force = 1;
          continue;
        }
      }
      err = zsv_printerr(1, "Unrecognized option: %s", arg);
    }

    if(!err) {
      switch(mode) {
      case zsv_property_mode_show_all:
        err = show_all_properties(filepath);
        break;
      case zsv_property_mode_detect:
        err = detect_properties(filepath, detect_opts);
        break;
      case zsv_property_mode_unset_all:
        err = unset_all_properties(filepath);
        break;
      case zsv_property_mode_set_unset:
        err = set_unset_property(filepath, cmd, property_id, value);
        show_all_properties(filepath);
        break;
      }
    }
  }

  return err;
}
