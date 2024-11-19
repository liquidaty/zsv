#include <unistd.h> // unlink()
#include <pthread.h>
#include <zsv/utils/index.h>
#include "index.h"

struct zsvsheet_ui_buffer {
  char *filename;
  char *data_filename;              // if this dataset was filtered from another, the filtered data is stored here
  struct zsv_opts zsv_opts;         // options to use when opening this file
  struct zsvsheet_ui_buffer *prior; // previous buffer in this stack. If null, this is the first buffer in the stack
  struct zsvsheet_screen_buffer_opts *buff_opts;
  zsvsheet_screen_buffer_t buffer;
  size_t cursor_row;
  size_t cursor_col;
  struct zsvsheet_input_dimensions dimensions;
  struct zsv_index *index;
  struct zsvsheet_index_opts *ixopts;
  pthread_mutex_t mutex;

  // input_offset: location within the input from which the buffer is read
  // i.e. if row = 5, col = 3, the buffer data starts from cell D6
  struct zsvsheet_rowcol input_offset;

  // buff_offset: location within the buffer from which the data is
  // displayed on the screen
  struct zsvsheet_rowcol buff_offset;
  size_t buff_used_rows;
  char *status;
  char *row_filter;

  void *ext_ctx; // extension context via zsvsheet_ext_set_ctx() zsvsheet_ext_get_ctx()

  // cleanup callback set by zsvsheet_ext_get_ctx()
  // if non-null, called when buffer is closed
  void (*ext_on_close)(void *);

  unsigned char index_ready;
  unsigned char rownum_col_offset : 1;
  unsigned char index_started : 1;
  unsigned char has_row_num : 1;
  unsigned char mutex_inited : 1;
  unsigned char _ : 4;
};

void zsvsheet_ui_buffer_delete(struct zsvsheet_ui_buffer *ub) {
  if (ub) {
    if (ub->ext_on_close)
      ub->ext_on_close(ub->ext_ctx);
    zsvsheet_screen_buffer_delete(ub->buffer);
    if (ub->mutex_inited)
      pthread_mutex_destroy(ub->mutex);
    if (ub->ixopts)
      ub->ixopts->uib = NULL;
    free(ub->row_filter);
    zsv_index_delete(ub->index);
    free(ub->status);
    if (ub->data_filename)
      unlink(ub->data_filename);
    free(ub->data_filename);
    free(ub->filename);
    free(ub);
  }
}

struct zsvsheet_ui_buffer_opts {
  struct zsvsheet_screen_buffer_opts *buff_opts;
  const char *row_filter;
  const char *filename;
  struct zsv_opts zsv_opts; // options to use when opening this file
  char no_rownum_col_offset;
};

struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_new(zsvsheet_screen_buffer_t buffer,
                                                  struct zsvsheet_ui_buffer_opts *uibopts) {
  struct zsvsheet_ui_buffer *uib = calloc(1, sizeof(*uib));
  pthread_mutex_t init = PTHREAD_MUTEX_INITIALIZER;
  if (uib) {
    uib->buffer = buffer;
    uib->mutex_inited = 1;
    memcpy(&uib->mutex, &init, sizeof(init));
    if (!(uibopts && uibopts->no_rownum_col_offset))
      uib->rownum_col_offset = 1;
    if (uibopts) {
      if ((uibopts->row_filter && !(uib->row_filter = strdup(uibopts->row_filter))) ||
          (uibopts->filename && !(uib->filename = strdup(uibopts->filename)))) {
        zsvsheet_ui_buffer_delete(uib);
        return NULL;
      }
      uib->zsv_opts = uibopts->zsv_opts;
    }
  }
  return uib;
}

enum zsvsheet_priv_status zsvsheet_ui_buffer_new_blank(struct zsvsheet_ui_buffer **uibp) {
  enum zsvsheet_priv_status status;
  zsvsheet_screen_buffer_t buffer = zsvsheet_screen_buffer_new(1, NULL, &status);
  if (buffer) {
    *uibp = zsvsheet_ui_buffer_new(buffer, NULL);
    return zsvsheet_priv_status_ok;
  }
  return zsvsheet_priv_status_error;
}

void zsvsheet_ui_buffers_delete(struct zsvsheet_ui_buffer *ub) {
  for (struct zsvsheet_ui_buffer *prior; ub; ub = prior) {
    prior = ub->prior;
    zsvsheet_ui_buffer_delete(ub);
  }
}

void zsvsheet_ui_buffer_push(struct zsvsheet_ui_buffer **base, struct zsvsheet_ui_buffer **current,
                             struct zsvsheet_ui_buffer *element) {
  if (*base == NULL)
    *base = *current = element;
  else {
    element->prior = *current;
    *current = element;
  }
}

/*
 * zsvsheet_ui_buffer_pop: pop the top off the stack
 * return 1 if popped, 0 if nothing popped
 * if @popped is non-NULL, set it to the popped element
 * otherwise delete the popped element
 */
int zsvsheet_ui_buffer_pop(struct zsvsheet_ui_buffer **base, struct zsvsheet_ui_buffer **current,
                           struct zsvsheet_ui_buffer **popped) {
  if (*current) {
    struct zsvsheet_ui_buffer *old_current = *current;
    if (old_current->prior)
      *current = old_current->prior;
    else
      *base = NULL;
    if (popped)
      *popped = old_current;
    else
      zsvsheet_ui_buffer_delete(old_current);
    return 1;
  }
  return 0;
}
