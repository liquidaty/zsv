int zsvsheet_ui_buffer_open_file(const char *filename, const struct zsv_opts *zsv_optsp, const char *row_filter,
                                 struct zsv_prop_handler *custom_prop_handler, const char *opts_used,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_top) {
  struct zsvsheet_buffer_opts bopts = {0};
  struct zsvsheet_ui_buffer_opts uibopts = {0};
  uibopts.filename = filename;
  if (zsv_optsp)
    uibopts.zsv_opts = *zsv_optsp;
  uibopts.buff_opts = &bopts;
  struct zsvsheet_opts zsvsheet_opts = {0};
  int err = 0;
  struct zsvsheet_ui_buffer *tmp_ui_buffer = NULL;
  uibopts.row_filter = row_filter;
  if ((err = read_data(&tmp_ui_buffer, &uibopts, 0, 0, 0, NULL, &zsvsheet_opts, custom_prop_handler, opts_used)) != 0 ||
      !tmp_ui_buffer || !tmp_ui_buffer->buff_used_rows) {
    zsvsheet_ui_buffer_delete(tmp_ui_buffer);
    if (err)
      return err;
    return -1;
  }
  tmp_ui_buffer->cursor_row = 1; // first row is header
  zsvsheet_ui_buffer_push(ui_buffer_stack_bottom, ui_buffer_stack_top, tmp_ui_buffer);
  return 0;
}

/****** API ******/

/**
 * Open a tabular file as a new buffer
 */
zsvsheet_handler_status zsvsheet_handler_open_file(zsvsheet_handler_context_t h, const char *filepath,
                                                   struct zsv_opts *zopts) {
  struct zsvsheet_handler_context *ctx = h;
  if (!ctx || !ctx->ui_buffers.base || !ctx->ui_buffers.current)
    return zsvsheet_handler_status_error;
  int err =
    zsvsheet_ui_buffer_open_file(filepath, zopts, NULL, NULL, NULL, ctx->ui_buffers.base, ctx->ui_buffers.current);
  if (err)
    return zsvsheet_handler_status_error;
  return zsvsheet_handler_status_ok;
}
