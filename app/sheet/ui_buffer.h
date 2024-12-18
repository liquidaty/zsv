#ifndef ZSV_UI_BUFFER_H
#define ZSV_UI_BUFFER_H

#include <stddef.h>

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
static inline void atomic_set_bit(volatile unsigned char *addr, int bit) {
  __sync_or_and_fetch(addr, 1U << bit);
}

static inline void atomic_clear_bit(volatile unsigned char *addr, int bit) {
  __sync_and_and_fetch(addr, ~(1U << bit));
}

static inline int atomic_test_bit(const volatile unsigned char *addr, int bit) {
  return !!(__sync_fetch_and_or((volatile unsigned char *)addr, 0) & (1U << bit));
}

// Flag structure for atomic operations
struct zsvsheet_ui_flags {
  volatile unsigned char flags[2]; // Using 2 bytes to accommodate all bits
};

// Function declarations
struct zsvsheet_ui_buffer;
typedef struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_t;

void zsvsheet_ui_buffer_create_worker(struct zsvsheet_ui_buffer *ub, void *(*start_func)(void *), void *arg);
void zsvsheet_ui_buffer_join_worker(struct zsvsheet_ui_buffer *ub);
void zsvsheet_ui_buffer_delete(struct zsvsheet_ui_buffer *ub);

#endif /* ZSV_UI_BUFFER_H */
