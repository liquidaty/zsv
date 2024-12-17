#ifndef ZSVSHEET_HANDLER_INTERNAL_H
#define ZSVSHEET_HANDLER_INTERNAL_H

struct zsvsheet_context {
  const char *subcommand_value; // e.g. "/path/to/myfile.csv"
  int ch;                       // key press value from getch()
  struct {
    struct zsvsheet_ui_buffer **base;
    struct zsvsheet_ui_buffer **current;
  } ui_buffers;
  const struct zsvsheet_display_dimensions *display_dims;
  struct zsv_prop_handler *custom_prop_handler;
};

struct zsvsheet_subcommand_context {
  int ch; // keyboard shortcut
  struct zsvsheet_ui_buffer *ui_buffers;
  struct zsvsheet_ui_buffer *current_ui_buffer;
  char prompt[256];
};

struct zsvsheet_key_data {
  struct zsvsheet_key_data *next;
  int ch;          // from getch()
  char *long_name; // command name that can be manually invoked via run-command <cmd>
  zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_context_t);
  zsvsheet_status (*handler)(zsvsheet_context_t);
};

/**
 * Open a tabular file as a new buffer
 */
zsvsheet_status zsvsheet_open_file(struct zsvsheet_proc_context *ctx, const char *filepath, struct zsv_opts *zopts);

/** extension support **/

/**
 * Set the subcommand prompt
 */
zsvsheet_status zsvsheet_subcommand_prompt(zsvsheet_subcommand_context_t ctx, const char *fmt, ...);

/**
 * Set a status message
 */

zsvsheet_status zsvsheet_set_status(struct zsvsheet_proc_context *ctx, const char *fmt, ...);

/**
 * Get the key press that triggered this subcommand handler
 */
int zsvsheet_ext_keypress(zsvsheet_proc_context_t);

/**
 * Get the current buffer
 */
zsvsheet_buffer_t zsvsheet_buffer_current(struct zsvsheet_proc_context *ctx);

/**
 * Get the prior buffer
 */
zsvsheet_buffer_t zsvsheet_buffer_prior(zsvsheet_buffer_t);

/**
 * Get the filename associated with a buffer
 */
const char *zsvsheet_buffer_filename(zsvsheet_buffer_t h);

/**
 * Get the data file associated with a buffer. This might not be the same as the filename,
 * such as when the data has been filtered
 */
const char *zsvsheet_buffer_data_filename(zsvsheet_buffer_t h);

/**
 * Set custom context
 * @param on_close optional callback to invoke when the buffer is closed
 *
 * @return zsv_ext_status_ok on success, else zsv_ext_status error code
 */
enum zsv_ext_status zsvsheet_buffer_set_ctx(zsvsheet_buffer_t h, void *ctx, void (*on_close)(void *));

/**
 * Get custom context previously set via zsvsheet_buffer_set_ctx()
 * @param ctx_out result will be written to this address
 *
 * @return zsv_ext_status_ok on success, else zsv_ext_status error code
 */
enum zsv_ext_status zsvsheet_buffer_get_ctx(zsvsheet_buffer_t h, void **ctx_out);

/**
 * Set custom handler on Enter key press
 *
 * @return zsv_ext_status_ok on success, else zsv_ext_status error code
 */
enum zsv_ext_status zsvsheet_buffer_on_newline(zsvsheet_buffer_t h,
                                               zsvsheet_status (*on_newline)(zsvsheet_proc_context_t));

zsvsheet_status zsvsheet_buffer_get_selected_cell(zsvsheet_buffer_t h, struct zsvsheet_rowcol *rc);

/**
 * Set custom cell attributes
 */
void zsvsheet_buffer_set_cell_attrs(zsvsheet_buffer_t h,
                                    enum zsv_ext_status (*get_cell_attrs)(void *ext_ctx, zsvsheet_cell_attr_t *,
                                                                          size_t start_row, size_t row_count,
                                                                          size_t col_count));

/** Get zsv_opts use to open the buffer's data file **/
struct zsv_opts zsvsheet_buffer_get_zsv_opts(zsvsheet_buffer_t h);

/**
 * Register a custom sheet command
 */
zsvsheet_status zsvsheet_register_command(int ch, const char *long_name,
                                          zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_context_t),
                                          zsvsheet_status (*handler)(zsvsheet_context_t));

/**
 * Transform the current buffer's underlying file into a new one and open the new file in a buffer
 */
enum zsvsheet_status zsvsheet_push_transformation(zsvsheet_proc_context_t ctx,
                                                  struct zsvsheet_buffer_transformation_opts opts);
#endif

struct zsvsheet_buffer_data zsvsheet_buffer_info(zsvsheet_buffer_t buff);

/** cell formatting **/
zsvsheet_cell_attr_t zsvsheet_cell_profile_attrs(enum zsvsheet_cell_profile_t);
