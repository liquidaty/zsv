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
  int ch;
  struct zsvsheet_ui_buffer *ui_buffers;
  struct zsvsheet_ui_buffer *current_ui_buffer;
  char prompt[256];
};

struct zsvsheet_key_handler_data {
  struct zsvsheet_key_handler_data *next;
  int ch; // from getch()
  enum zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t);
  enum zsvsheet_status (*handler)(zsvsheet_handler_context_t);
};

#endif
