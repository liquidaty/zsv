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
 * Load cached file properties
 * @param data_filepath required file path
 * @param opts parser options to modify
 * @param fp (optional) parsed file properties
 * @param cmd_opts_used (optional) string of cmd options already used
 * @return 0 on success, else error code
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
 * ignored), but a warning is printed
 */
enum zsv_status zsv_new_with_properties(struct zsv_opts *opts,
                                        const char *input_path,
                                        const char *opts_used,
                                        zsv_parser *handle_out
                                        );

#endif
