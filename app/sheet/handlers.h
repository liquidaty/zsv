#ifndef ZSVSHEET_HANDLER_H
#define ZSVSHEET_HANDLER_H

struct zsvsheet_handler_context {
  const char *subcommand_value; // e.g. "/path/to/myfile.csv"
  char ch;                      // key press value from getch()
  struct {
    struct zsvsheet_ui_buffer **base;
    struct zsvsheet_ui_buffer **current;
  } ui_buffers;
  const struct zsvsheet_display_dimensions *display_dims;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

struct zsvsheet_key_handler_data {
  enum zsvsheet_status (*subcommand_handler)(int ch, struct zsvsheet_ui_buffer *ui_buffers,
                                             struct zsvsheet_ui_buffer *current_ui_buffer, char *buff, size_t buff_sz);
  enum zsvsheet_status (*handler)(struct zsvsheet_handler_context *ctx);
};

#endif
