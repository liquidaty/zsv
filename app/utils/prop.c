#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/cache.h>
#include <zsv/utils/file.h>
#include <yajl_helper.h>

// to do: import these through a proper header
static int zsv_properties_parse_process_value(struct yajl_helper_parse_state *st, struct json_value *value);
unsigned char *zsv_cache_filepath(const unsigned char *data_filepath,
                                  enum zsv_cache_type type, char create_dir,
                                  char temp_file);

struct zsv_properties_parser {
  struct yajl_helper_parse_state st;
  yajl_status stat;
};

/**
 * Create a new properties parser
 */
struct zsv_properties_parser *zsv_properties_parser_new(struct zsv_file_properties *fp) {
  struct zsv_properties_parser *parser = calloc(1, sizeof(*parser));
  if(parser) {
    parser->stat =
      yajl_helper_parse_state_init(&parser->st, 32,
                                   NULL, // start_map,
                                   NULL, // end_map,
                                   NULL, // map_key,
                                   NULL, // start_array,
                                   NULL, // end_array,
                                   zsv_properties_parse_process_value,
                                   fp);
  }
  return parser;
}

/**
 * Finished parsing
 */
enum zsv_status zsv_properties_parse_complete(struct zsv_properties_parser *parser) {
  if(parser && parser->st.yajl) {
    if(parser->stat == yajl_status_ok)
      parser->stat = yajl_complete_parse(parser->st.yajl);
  }
  return parser->stat == yajl_status_ok ? zsv_status_ok : zsv_status_error;
}

/**
 * Clean up
 */
enum zsv_status zsv_properties_parser_destroy(struct zsv_properties_parser *parser) {
  yajl_helper_parse_state_free(&parser->st);
  yajl_status stat = parser->stat;
  free(parser);
  return stat == yajl_status_ok ? zsv_status_ok : zsv_status_error;

}

/**
 * Load cached file properties into a zsp_opts and/or zsv_file_properties struct
 * If cmd_opts_used is provided, then do not set any zsv_opts values, if the
 * corresponding option code is already present in cmd_opts_used, and instead
 * print a warning to stderr
 *
 * @param data_filepath            required file path
 * @param opts (optional)          parser options to load
 * @param fp (optional)            parsed file properties
 * @param cmd_opts_used (optional) cmd option codes to skip + warn if found
 * @return zsv_status_ok on success
 */
enum zsv_status zsv_cache_load_props(const char *data_filepath,
                                     struct zsv_opts *opts,
                                     struct zsv_file_properties *fp,
                                     const char *cmd_opts_used) {
  // we need some memory to save the parsed properties
  // if the caller did not provide that, use our own
  struct zsv_file_properties tmp = { 0 };
  if(!fp)
    fp = &tmp;
  if(!(data_filepath && *data_filepath)) return 0; // e.g. input = stdin

  enum zsv_status stat = zsv_status_ok;
  struct zsv_properties_parser *p = NULL;
  unsigned char *fn = zsv_cache_filepath((const unsigned char *)data_filepath,
                                         zsv_cache_type_property, 0, 0);
  if(!fn)
    stat = zsv_status_memory;
  else {
    FILE *f;
    int err;
    if(!zsv_file_readable((char *)fn, &err, &f)) {
      if(err != ENOENT) {
        perror((const char *)fn);
        stat = zsv_status_error;
      }
    } else {
      p = zsv_properties_parser_new(fp);
      if(!p)
        stat = zsv_status_memory;
      else if(p->stat != yajl_status_ok)
        stat = zsv_status_error;
      else {
        unsigned char buff[1024];
        size_t bytes_read;
        while((bytes_read = fread(buff, 1, sizeof(buff), f))) {
          if((p->stat = yajl_parse(p->st.yajl, buff, bytes_read)) != yajl_status_ok) {
            stat = zsv_status_error;
            break;
          }
        }
        if(stat == zsv_status_ok)
          zsv_properties_parse_complete(p);
      }
      fclose(f);
    }
    free(fn);
  }

  if(stat == zsv_status_ok) {
    // warn if the loaded properties conflict with command-line options
    if(fp->skip_specified) {
      if(cmd_opts_used && strchr(cmd_opts_used, 'R'))
        fprintf(stderr, "Warning: file property 'skip-head' overridden by command option\n");
      else if(opts)
        opts->rows_to_ignore = fp->skip;
    }
    if(fp->header_span_specified) {
      if(cmd_opts_used && strchr(cmd_opts_used, 'd'))
        fprintf(stderr, "Warning: file property 'header-row-span' overridden by command option\n");
      else if(opts)
        opts->header_span = fp->header_span;
    }
  }
  if(p && stat == zsv_status_ok
     && zsv_properties_parser_destroy(p) != zsv_status_ok
     )
    stat = zsv_status_error;
  return stat;
}

static int zsv_properties_parse_process_value(struct yajl_helper_parse_state *st, struct json_value *value) {
  struct zsv_file_properties *fp = st->data;
  if(st->level == 1) {
    const char *prop_name = yajl_helper_get_map_key(st, 0);
    unsigned int *target = NULL;
    if(!strcmp(prop_name, "skip-head")) {
      target = &fp->skip;
      fp->skip_specified = 1;
    } else if(!strcmp(prop_name, "header-row-span")) {
      target = &fp->header_span;
      fp->header_span_specified = 1;
    }
    if(!target) {
      fprintf(stderr, "Unrecognized property: %s\n", prop_name);
      fp->err = 1;
    } else {
      long long i = json_value_long(value, &fp->err);
      if(fp->err || i < 0 || i > UINT_MAX)
        fprintf(stderr, "Invalid %s property value: should be an integer between 0 and %u", prop_name, UINT_MAX);
      else
        *target = (unsigned int) i;
    }
  }
  return 1;
}

/**
 * zsv_new_with_properties(): use in lieu of zsv_new() to also merge zsv options
 * with any saved properties (such as rows_to_ignore or header_span) for the
 * specified input file. In the event that saved properties conflict with a
 * command-line option, the command-line option "wins" (the property value is
 * ignored), but a warning is printed
 */
enum zsv_status zsv_new_with_properties(struct zsv_opts *opts,
                                        const char *input_path,
                                        const char *opts_used,
                                        zsv_parser *handle_out
                                        ) {
  enum zsv_status stat = zsv_status_ok;
  *handle_out = NULL;
  if(input_path) {
    stat = zsv_cache_load_props(input_path, opts, NULL, opts_used);
    if(stat != zsv_status_ok)
      return stat;
  }
  if((*handle_out = zsv_new(opts)))
    return zsv_status_ok;
  return zsv_status_memory;
}
