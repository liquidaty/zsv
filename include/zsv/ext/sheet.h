#ifndef ZSVSHEET_H
#define ZSVSHEET_H

enum zsvsheet_status {
  zsvsheet_status_ok = 0,
  zsvsheet_status_memory,
  zsvsheet_status_error, // generic error
  zsvsheet_status_utf8,
  zsvsheet_status_continue, // ignore / continue
  zsvsheet_status_duplicate
};

typedef struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_t;
typedef struct zsvsheet_handler_context *zsvsheet_handler_context_t;
typedef struct zsvsheet_subcommand_handler_context *zsvsheet_subcommand_handler_context_t;

enum zsvsheet_status zsvsheet_register_key_handler(
  int ch, enum zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  enum zsvsheet_status (*handler)(zsvsheet_handler_context_t ctx));

/*
 * Set the prompt for entering a subcommand
 * @param  s text to set the subcommand prompt to. must be < 256 bytes in length
 * returns zsvsheet_status_ok on success
 */
enum zsvsheet_status zsvsheet_subcommand_set_prompt(zsvsheet_subcommand_handler_context_t ctx, const char *s);

#endif
