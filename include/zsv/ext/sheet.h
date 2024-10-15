#ifndef ZSVSHEET_H
#define ZSVSHEET_H

typedef enum zsvsheet_handler_status {
  zsvsheet_handler_status_ok = 0,
  zsvsheet_handler_status_error,
  zsvsheet_handler_status_ignore,
  zsvsheet_handler_status_duplicate,
  zsvsheet_handler_status_memory
} zsvsheet_handler_status;

typedef struct zsvsheet_handler_context *zsvsheet_handler_context_t;
typedef struct zsvsheet_subcommand_handler_context *zsvsheet_subcommand_handler_context_t;

typedef void *zsvsheet_handler_buffer_t;

#endif
