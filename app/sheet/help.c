struct zsvsheet_help_data {
  size_t rows_fetched;
  const char *row_data[3];
};

const unsigned char **zsvsheet_get_help_header(void *d) {
  struct zsvsheet_help_data *data = d;
  data->row_data[0] = "Key(s)";
  data->row_data[1] = "Action";
  data->row_data[2] = "Description";
  return (const unsigned char **)data->row_data;
}

const unsigned char *zsvsheet_get_help_status(void *_) {
  (void)(_);
  return (const unsigned char *)"<esc> to exit help";
}

const unsigned char **zsvsheet_get_help_row(void *d) {
  struct zsvsheet_help_data *data = d;
  while (1) {
    size_t i = data->rows_fetched++;
    if (zsvsheet_get_key_binding(i) == NULL)
      return NULL;

    struct zsvsheet_key_binding *kb = zsvsheet_get_key_binding(i);
    struct zsvsheet_procedure *proc = zsvsheet_find_procedure(kb->proc_id);
    if (proc == NULL || kb->hidden)
      continue;

    data->row_data[0] = zsvsheet_key_binding_ch_name(kb);
    data->row_data[1] = proc->name;
    data->row_data[2] = proc->description;
    return (const unsigned char **)data->row_data;
  }
}

static zsvsheet_status zsvsheet_help_handler(struct zsvsheet_proc_context *ctx) {
  const size_t cols = 3;
  struct zsvsheet_help_data d = {0};
  return zsvsheet_buffer_new_static(ctx, cols, zsvsheet_get_help_header, zsvsheet_get_help_row,
                                    zsvsheet_get_help_status, &d, NULL);
}
