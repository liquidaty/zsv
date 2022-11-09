#ifndef ZSV_PROP_H
#define ZSV_PROP_H

struct zsv_file_properties {
  unsigned int skip;
  unsigned int header_span;
  int err;

  /* flags used by parser only to indicate whether property was specified */
  unsigned int skip_specified:1;
  unsigned int header_span_specified:1;
  unsigned int _:6;
};

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
                                     const char *cmd_opts_used);

/**
 * Create a new properties parser
 */
struct zsv_properties_parser;
struct zsv_properties_parser *zsv_properties_parser_new(struct zsv_file_properties *fp);

/**
 * Finished parsing
 */
enum zsv_status zsv_properties_parse_complete(struct zsv_properties_parser *parser);

/**
 * Clean up
 */
enum zsv_status zsv_properties_parser_destroy(struct zsv_properties_parser *parser);

/**
 * zsv_new_with_properties(): use in lieu of zsv_new() to also merge zsv options
 * with any saved properties (such as rows_to_ignore or header_span) for the
 * specified input file. In the event that saved properties conflict with a
 * command-line option, the command-line option "wins" (the property value is
 * ignored), but a warning is printed.
 *
 * @param handle_out returns zsv parser handle, or NULL on fail
 */
enum zsv_status zsv_new_with_properties(struct zsv_opts *opts,
                                        const char *input_path,
                                        const char *opts_used,
                                        zsv_parser *handle_out
                                        );
#endif
