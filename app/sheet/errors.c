struct zsvsheet_errors_data {
  struct zsvsheet_ui_buffer *uib;
  size_t rows_fetched;
  const char *row_data[1];
};

const unsigned char **zsvsheet_errors_header(void *d) {
  struct zsvsheet_errors_data *data = d;
  data->row_data[0] = "Error";
  return (const unsigned char **)data->row_data;
}

const unsigned char *zsvsheet_errors_status(void *_) {
  (void)(_);
  return (const unsigned char *)"<esc> to exit, then :errors-clear to clear";
}

const unsigned char **zsvsheet_errors_row(void *d) {
  struct zsvsheet_errors_data *data = d;
  while (data->rows_fetched < data->uib->parse_errs.count) {
    data->row_data[0] = data->uib->parse_errs.errors[data->rows_fetched];
    data->rows_fetched++;
    if (data->row_data[0] && *data->row_data[0])
      return (const unsigned char **)data->row_data;
  }
  return NULL;
}

static zsvsheet_status zsvsheet_errors_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_errors_data d = {0};
  d.uib = zsvsheet_buffer_current(ctx);
  if (!d.uib)
    return zsvsheet_status_error;
  if (ctx->proc_id == zsvsheet_builtin_proc_errors_clear) {
    uib_parse_errs_clear(&d.uib->parse_errs);
    return zsvsheet_status_ok;
  }
  if (d.uib->parse_errs.count == 0) {
    zsvsheet_ui_buffer_set_status(d.uib, "No errors");
    return zsvsheet_status_ok;
  }
  const size_t cols = 1;
  return zsvsheet_buffer_new_static(ctx, cols, zsvsheet_errors_header, zsvsheet_errors_row, zsvsheet_errors_status, &d,
                                    NULL);
}
