struct zsvsheet_ui_buffer {
  struct zsvsheet_ui_buffer *prior; // previous buffer in this stack. If null, this is the first buffer in the stack
  struct zsvsheet_buffer_opts *buff_opts;
  zsvsheet_buffer_t buffer;
  size_t cursor_row;
  size_t cursor_col;
  struct zsvsheet_input_dimensions dimensions;
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_t mutex;
#endif
  // input_offset: location within the input from which the buffer is read
  // i.e. if row = 5, col = 3, the buffer data starts from cell D6
  struct zsvsheet_rowcol input_offset;

  // buff_offset: location within the buffer from which the data is
  // displayed on the screen
  struct zsvsheet_rowcol buff_offset;
  size_t buff_used_rows;
  char *status;
  char *row_filter;

  unsigned char indexed : 1;
  unsigned char _ : 7;
};

void zsvsheet_ui_buffer_delete(struct zsvsheet_ui_buffer *ub) {
  if (ub) {
    zsvsheet_buffer_delete(ub->buffer);
    free(ub->row_filter);
    free(ub->status);
    free(ub);
  }
}

struct zsvsheet_ui_buffer_opts {
  struct zsvsheet_buffer_opts *buff_opts;
  const char *row_filter;
};

struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_new(zsvsheet_buffer_t buffer, struct zsvsheet_ui_buffer_opts *uibopts) {
  struct zsvsheet_ui_buffer *uib = calloc(1, sizeof(*uib));
  if(uib) {
    uib->buffer = buffer;
#ifdef ZSVSHEET_USE_THREADS
    uib->mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
    if (uibopts && uibopts->row_filter && !(uib->row_filter = strdup(uibopts->row_filter))) {
      zsvsheet_ui_buffer_delete(uib);
      return NULL;
    }
  }
  return uib;
}

enum zsvsheet_status zsvsheet_ui_buffer_new_blank(struct zsvsheet_ui_buffer **uibp) {
  enum zsvsheet_status status;
  zsvsheet_buffer_t buffer = zsvsheet_buffer_new(1, NULL, &status);
  if(buffer) {
    *uibp = zsvsheet_ui_buffer_new(buffer, NULL);
    return zsvsheet_status_ok;
  }
  return zsvsheet_status_error;
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

struct zsvsheet_ui_buffer *zsvsheet_ui_buffer_pop(struct zsvsheet_ui_buffer **base,
                                                  struct zsvsheet_ui_buffer **current) {
  if (*current) {
    struct zsvsheet_ui_buffer *old_current = *current;
    if (old_current->prior)
      *current = old_current->prior;
    else
      *base = NULL;
    return old_current;
  }
  return NULL;
}

