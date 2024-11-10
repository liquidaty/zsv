#ifndef ZSVSHEET_HANDLER_INTERNAL_H
#define ZSVSHEET_HANDLER_INTERNAL_H

struct zsvsheet_handler_context {
  const char *subcommand_value; // e.g. "/path/to/myfile.csv"
  int ch;                       // key press value from getch()
  struct {
    struct zsvsheet_ui_buffer **base;
    struct zsvsheet_ui_buffer **current;
  } ui_buffers;
  const struct zsvsheet_display_dimensions *display_dims;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

struct zsvsheet_subcommand_handler_context {
  int ch; // keyboard shortcut
  struct zsvsheet_ui_buffer *ui_buffers;
  struct zsvsheet_ui_buffer *current_ui_buffer;
  char prompt[256];
};

struct zsvsheet_key_handler_data {
  struct zsvsheet_key_handler_data *next;
  int ch;          // from getch()
  char *long_name; // command name that can be manually invoked via run-command <cmd>
  zsvsheet_handler_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t);
  zsvsheet_handler_status (*handler)(zsvsheet_handler_context_t);
};

/**
 * Open a tabular file as a new buffer
 */
zsvsheet_handler_status zsvsheet_handler_open_file(struct zsvsheet_proc_context *ctx, const char *filepath,
                                                   struct zsv_opts *zopts);

/** extension support **/

/**
 * Set the subcommand prompt
 */
zsvsheet_handler_status zsvsheet_subcommand_prompt(zsvsheet_subcommand_handler_context_t ctx, const char *fmt, ...);

/**
 * Set a status message
 */

zsvsheet_handler_status zsvsheet_handler_set_status(struct zsvsheet_proc_context *ctx, const char *fmt, ...);

/**
 * Get the key press that triggered this subcommand handler
 */
int zsvsheet_ext_keypress(zsvsheet_proc_context_t);

/**
 * Get the current buffer
 */
zsvsheet_handler_buffer_t zsvsheet_handler_buffer_current(struct zsvsheet_proc_context *ctx);

/**
 * Get the prior buffer
 */
zsvsheet_handler_buffer_t zsvsheet_handler_buffer_prior(zsvsheet_handler_buffer_t);

/**
 * Get the filename associated with a buffer
 */
const char *zsvsheet_handler_buffer_filename(zsvsheet_handler_buffer_t h);

/**
 * Get the data file associated with a buffer. This might not be the same as the filename,
 * such as when the data has been filtered
 */
const char *zsvsheet_handler_buffer_data_filename(zsvsheet_handler_buffer_t h);

/**
 * Set custom context
 * @param on_close optional callback to invoke when the buffer is closed
 */
void zsvsheet_buffer_set_ctx(zsvsheet_handler_buffer_t h, void *ctx, void (*on_close)(void *));

/**
 * Get custom context previously set via zsvsheet_buffer_set_ctx()
 */
void *zsvsheet_buffer_get_ctx(zsvsheet_handler_buffer_t h);

/**
 * Register a custom sheet command
 */
zsvsheet_handler_status zsvsheet_register_command(
  int ch, const char *long_name, zsvsheet_handler_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  zsvsheet_handler_status (*handler)(zsvsheet_handler_context_t));

#endif
