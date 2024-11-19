#ifndef ZSVSHEET_H
#define ZSVSHEET_H

/* custom sheet handler id */
typedef int zsvsheet_proc_id_t;

typedef struct zsvsheet_proc_context *zsvsheet_proc_context_t;

typedef enum zsvsheet_status {
  zsvsheet_status_ok = 0,
  zsvsheet_status_error,
  zsvsheet_status_ignore,
  zsvsheet_status_duplicate,
  zsvsheet_status_memory,
  zsvsheet_status_exit,
} zsvsheet_status;

typedef struct zsvsheet_context *zsvsheet_context_t;
typedef struct zsvsheet_subcommand_context *zsvsheet_subcommand_context_t;

typedef void *zsvsheet_buffer_t;
// int zsvsheet_ext_keypress(zsvsheet_proc_context_t);

typedef struct zsvsheet_transformation *zsvsheet_transformation;

#endif
