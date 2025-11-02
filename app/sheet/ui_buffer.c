#include <unistd.h> // unlink()
#include <pthread.h>
#include "../utils/index.h"
#include "index.h"

struct uib_parse_errs {
  size_t count;
  size_t max;
  char **errors;
};

static void uib_parse_errs_init(struct uib_parse_errs *p, size_t max) {
  memset(p, 0, sizeof(*p));
  p->max = max;
}

static void uib_parse_errs_clear(struct uib_parse_errs *parse_errs) {
  for (size_t i = 0; i < parse_errs->count; i++)
    free(parse_errs->errors[i]);
  free(parse_errs->errors);

  // reset so it can be used again
  size_t max = parse_errs->max;
  uib_parse_errs_init(parse_errs, max);
}

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
  pthread_t worker_thread;

  // input_offset: location within the input from which the buffer is read
  // i.e. if row = 5, col = 3, the buffer data starts from cell D6
  struct zsvsheet_rowcol input_offset;

  // buff_offset: location within the buffer from which the data is
  // displayed on the screen
  struct zsvsheet_rowcol buff_offset;
  size_t buff_used_rows;
  char *status;

  void *ext_ctx; // extension context via zsvsheet_ext_set_ctx() zsvsheet_ext_get_ctx()

  // cleanup callback set by zsvsheet_ext_get_ctx()
  // if non-null, called when buffer is closed
  zsvsheet_status (*on_newline)(zsvsheet_proc_context_t);
  void (*ext_on_close)(void *);

  enum zsv_ext_status (*get_cell_attrs)(void *, zsvsheet_cell_attr_t *, size_t, size_t, size_t);

  struct uib_parse_errs parse_errs;

  unsigned char index_ready : 1;
  unsigned char rownum_col_offset : 1;
  unsigned char index_started : 1;
  unsigned char has_row_num : 1;
  unsigned char mutex_inited : 1;
  unsigned char write_in_progress : 1;
  unsigned char write_done : 1;
  unsigned char worker_active : 1;
  unsigned char worker_cancelled : 1;
  unsigned char _ : 7;
};

void zsvsheet_ui_buffer_create_worker(struct zsvsheet_ui_buffer *ub, void *(*start_func)(void *), void *arg) {
  assert(!ub->worker_active);
  assert(ub->mutex_inited);

  pthread_create(&ub->worker_thread, NULL, start_func, arg);
  ub->worker_active = 1;
}

void zsvsheet_ui_buffer_set_status(struct zsvsheet_ui_buffer *ub, const char *status) {
  free(ub->status);
  ub->status = status ? strdup(status) : NULL;
}

int zsvsheet_ui_buffer_index_ready(struct zsvsheet_ui_buffer *ub, char skip_lock) {
  if (!skip_lock)
    pthread_mutex_lock(&ub->mutex);
  int rc = !!ub->index_ready;
  if (!skip_lock)
    pthread_mutex_unlock(&ub->mutex);
  return rc;
}

void zsvsheet_ui_buffer_join_worker(struct zsvsheet_ui_buffer *ub) {
  assert(ub->worker_active);
  assert(ub->mutex_inited);

  pthread_join(ub->worker_thread, NULL);
  ub->worker_active = 0;
}

void zsvsheet_ui_buffer_delete(struct zsvsheet_ui_buffer *ub) {
  if (ub) {
    if (ub->worker_active) {
      pthread_mutex_lock(&ub->mutex);
      ub->worker_cancelled = 1;
      pthread_mutex_unlock(&ub->mutex);

      zsvsheet_ui_buffer_join_worker(ub);
    }
    if (ub->ext_on_close)
      ub->ext_on_close(ub->ext_ctx);
    zsvsheet_screen_buffer_delete(ub->buffer);
    if (ub->mutex_inited)
      pthread_mutex_destroy(&ub->mutex);
    if (ub->ixopts)
      ub->ixopts->uib = NULL;
    zsv_index_delete(ub->index);
    free(ub->status);
    if (ub->data_filename)
      unlink(ub->data_filename);
    uib_parse_errs_clear(&ub->parse_errs);
    free(ub->data_filename);
    free(ub->filename);
    free(ub);
  }
}

struct zsvsheet_ui_buffer_opts {
  struct zsvsheet_screen_buffer_opts *buff_opts;
  const char *filename;
  const char *data_filename;
  struct zsv_opts zsv_opts; // options to use when opening this file
  char no_rownum_col_offset;
  char write_after_open;
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
      if (uibopts->filename && !(uib->filename = strdup(uibopts->filename))) {
        zsvsheet_ui_buffer_delete(uib);
        return NULL;
      }
      if (uibopts->data_filename && !(uib->data_filename = strdup(uibopts->data_filename))) {
        zsvsheet_ui_buffer_delete(uib);
        return NULL;
      }
      uib->zsv_opts = uibopts->zsv_opts;
      uib->write_in_progress = uibopts->write_after_open;
    }
  }
  return uib;
}

int zsvsheet_ui_buffer_update_cell_attr(struct zsvsheet_ui_buffer *uib) {
  if (uib && uib->buffer && uib->buffer->opts.rows && uib->buffer->cols) {
    size_t row_sz = uib->buffer->cols * sizeof(*uib->buffer->cell_attrs);
    if (uib->get_cell_attrs) {
      if (!uib->buffer->cell_attrs) {
        uib->buffer->cell_attrs = calloc(uib->buffer->opts.rows, row_sz);
        if (!uib->buffer->cell_attrs)
          return ENOMEM;
      }
      memset(uib->buffer->cell_attrs, 0, uib->buffer->opts.rows * row_sz);
      uib->get_cell_attrs(uib->ext_ctx, uib->buffer->cell_attrs, uib->input_offset.row, uib->buff_used_rows,
                          uib->buffer->cols);
    }
  }
  return 0;
}

static int uib_parse_errs_printf(void *uib_parse_errs_ptr, const char *fmt, ...) {
  struct uib_parse_errs *parse_errs = uib_parse_errs_ptr;
  char *err_msg = NULL;
  int n = 0;
  if (parse_errs->count < parse_errs->max) {
    va_list argv;
    va_start(argv, fmt);
    n = vasprintf(&err_msg, fmt, argv);
    va_end(argv);
    if (!(n <= 0 || !err_msg || !*err_msg)) {
      if (!parse_errs->errors)
        parse_errs->errors = (char **)calloc(parse_errs->max + 1, sizeof(*parse_errs->errors));
      if (parse_errs->errors) {
        parse_errs->errors[parse_errs->count++] = err_msg;
        return n;
      }
    }
  } else if (parse_errs->count > 0 && parse_errs->count == parse_errs->max && parse_errs->errors) {
    err_msg = strdup("Additional errors ignored");
    if (err_msg) {
      parse_errs->errors[parse_errs->count++] = err_msg;
      return strlen(err_msg);
    }
  }
  free(err_msg);
  return n;
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

static const char *zsvsheet_ui_buffer_get_header(struct zsvsheet_ui_buffer *uib, size_t col) {
  struct zsvsheet_screen_buffer *sb = uib->buffer;

  return (char *)zsvsheet_screen_buffer_cell_display(sb, 0, col);
}
