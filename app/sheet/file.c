int zsvsheet_ui_buffer_open_file_opts(struct zsvsheet_ui_buffer_opts *uibopts,
                                      struct zsv_prop_handler *custom_prop_handler,
                                      struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                      struct zsvsheet_ui_buffer **ui_buffer_stack_top) {
  struct zsvsheet_opts zsvsheet_opts = {0};
  struct zsvsheet_screen_buffer_opts bopts = {0};
  struct zsvsheet_ui_buffer *tmp_ui_buffer = NULL;

  if (!uibopts->buff_opts)
    uibopts->buff_opts = &bopts;

  int err = 0;
  if ((err = read_data(&tmp_ui_buffer, uibopts, 0, 0, 0, &zsvsheet_opts, custom_prop_handler)) != 0 || !tmp_ui_buffer ||
      !tmp_ui_buffer->buff_used_rows) {
    zsvsheet_ui_buffer_delete(tmp_ui_buffer);
    if (err)
      return err;
    return -1;
  }
  tmp_ui_buffer->cursor_row = 1; // first row is header
  zsvsheet_ui_buffer_push(ui_buffer_stack_bottom, ui_buffer_stack_top, tmp_ui_buffer);

  return 0;
}

int zsvsheet_ui_buffer_open_file(const char *filename, const struct zsv_opts *zsv_optsp,
                                 struct zsv_prop_handler *custom_prop_handler,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_top) {
  struct zsvsheet_ui_buffer_opts uibopts = {0};
  uibopts.filename = filename;
  if (zsv_optsp)
    uibopts.zsv_opts = *zsv_optsp;

  return zsvsheet_ui_buffer_open_file_opts(&uibopts, custom_prop_handler, ui_buffer_stack_bottom, ui_buffer_stack_top);
}

/****** API ******/

/**
 * Open a tabular file as a new buffer
 */
zsvsheet_status zsvsheet_open_file(struct zsvsheet_proc_context *ctx, const char *filepath, struct zsv_opts *zopts) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  if (!di || !di->ui_buffers.base || !di->ui_buffers.current)
    return zsvsheet_status_error;
  int err = zsvsheet_ui_buffer_open_file(filepath, zopts, NULL, di->ui_buffers.base, di->ui_buffers.current);
  if (err)
    return zsvsheet_status_error;
  return zsvsheet_status_ok;
}

zsvsheet_status zsvsheet_open_file_opts(struct zsvsheet_proc_context *ctx, struct zsvsheet_ui_buffer_opts *opts) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  if (!di || !di->ui_buffers.base || !di->ui_buffers.current)
    return zsvsheet_status_error;
  int err = zsvsheet_ui_buffer_open_file_opts(opts, NULL, di->ui_buffers.base, di->ui_buffers.current);
  if (err)
    return zsvsheet_status_error;
  return zsvsheet_status_ok;
}
