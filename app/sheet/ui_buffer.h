#ifndef ZSV_UI_BUFFER_H
#define ZSV_UI_BUFFER_H

#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>

// Bit positions for flags
#define INDEX_READY_BIT 0
#define ROWNUM_COL_OFFSET_BIT 1
#define INDEX_STARTED_BIT 2
#define HAS_ROW_NUM_BIT 3
#define MUTEX_INITED_BIT 4
#define WRITE_IN_PROGRESS_BIT 5
#define WRITE_DONE_BIT 6
#define WORKER_ACTIVE_BIT 7
#define WORKER_CANCELLED_BIT 8

// Atomic bit field operations
static inline void atomic_set_bit(volatile atomic_uchar *addr, int bit) {
  atomic_fetch_or_explicit(addr, 1U << bit, memory_order_release);
}

static inline void atomic_clear_bit(volatile atomic_uchar *addr, int bit) {
  atomic_fetch_and_explicit(addr, ~(1U << bit), memory_order_release);
}

static inline int atomic_test_bit(const volatile atomic_uchar *addr, int bit) {
  return !!(atomic_load_explicit((volatile atomic_uchar *)addr, memory_order_acquire) & (1U << bit));
}

// Flag structure for atomic operations
struct zsvsheet_ui_flags {
  volatile atomic_uchar flags[2]; // Using 2 bytes to accommodate all bits
};

#include "sheet_internal.h"

// Buffer structure forward declaration
struct zsvsheet_ui_buffer;
typedef struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_t;
struct zsvsheet_buffer_info_internal;

// Row/column position structure
struct zsvsheet_rowcol {
  size_t row;
  size_t col;
};

// Buffer structure definition
struct zsvsheet_ui_buffer {
  struct zsvsheet_ui_flags flags;
  struct zsv_index *index;
  struct zsvsheet_input_dimensions dimensions;
  struct zsvsheet_ui_buffer *prior;
  const char *filename;
  const char *data_filename;
  pthread_mutex_t mutex;
  void *ext_ctx;
  void (*ext_on_close)(void *);
  const char *status;
  struct zsv_opts zsv_opts;
  size_t cursor_row;
  size_t cursor_col;
  struct zsvsheet_rowcol input_offset;
  struct zsvsheet_rowcol buff_offset;
  void *buffer; // screen buffer
  enum zsv_ext_status (*get_cell_attrs)(void *ext_ctx, zsvsheet_cell_attr_t *, size_t start_row, size_t row_count,
                                        size_t col_count);
  zsvsheet_status (*on_newline)(struct zsvsheet_proc_context *);
};

// Function declarations
void zsvsheet_ui_buffer_create_worker(struct zsvsheet_ui_buffer *ub, void *(*start_func)(void *), void *arg);
void zsvsheet_ui_buffer_join_worker(struct zsvsheet_ui_buffer *ub);
void zsvsheet_ui_buffer_delete(struct zsvsheet_ui_buffer *ub);

// Compatibility macros for struct access
#define has_row_num(b) atomic_test_bit(&(b)->flags.flags[0], HAS_ROW_NUM_BIT)
#define rownum_col_offset(b) atomic_test_bit(&(b)->flags.flags[0], ROWNUM_COL_OFFSET_BIT)
#define index_ready(b) atomic_test_bit(&(b)->flags.flags[0], INDEX_READY_BIT)
#define index_started(b) atomic_test_bit(&(b)->flags.flags[0], INDEX_STARTED_BIT)
#define write_in_progress(b) atomic_test_bit(&(b)->flags.flags[0], WRITE_IN_PROGRESS_BIT)
#define write_done(b) atomic_test_bit(&(b)->flags.flags[0], WRITE_DONE_BIT)

#endif /* ZSV_UI_BUFFER_H */
