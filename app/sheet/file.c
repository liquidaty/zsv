int zsvsheet_ui_buffer_open_file(const char *filename, const struct zsv_opts *zsv_optsp, const char *row_filter,
                                 struct zsv_prop_handler *custom_prop_handler, const char *opts_used,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_top) {
  struct zsvsheet_screen_buffer_opts bopts = {0};
  struct zsvsheet_ui_buffer_opts uibopts = {0};
  struct zsvsheet_ui_buffer *current_ui_buff = *ui_buffer_stack_top;
  uibopts.filename = filename;
  char in_filter = row_filter && current_ui_buff && current_ui_buff->filename == filename && current_ui_buff->data_filename != NULL;
  if(in_filter)
    uibopts.filename = current_ui_buff->data_filename;
  if (zsv_optsp)
    uibopts.zsv_opts = *zsv_optsp;
  uibopts.buff_opts = &bopts;
  struct zsvsheet_opts zsvsheet_opts = {0};
  int err = 0;
  struct zsvsheet_ui_buffer *new_ui_buffer = NULL;
  uibopts.row_filter = row_filter;
  if ((err = read_data(&new_ui_buffer, &uibopts, 0, 0, 0, &zsvsheet_opts, custom_prop_handler, opts_used)) != 0 ||
      !new_ui_buffer || !new_ui_buffer->buff_used_rows) {
    zsvsheet_ui_buffer_delete(new_ui_buffer);
    if (err)
      return err;
    return -1;
  }
  new_ui_buffer->cursor_row = 1; // first row is header
  if(current_ui_buff) {
    new_ui_buffer->rownum_display = current_ui_buff->rownum_display == zsvsheet_rownum_display_none ? zsvsheet_rownum_display_none :
      zsvsheet_rownum_display_in_data;
  }
  if(in_filter) {
     // in case it was previously set to current_ui_buff->data_filename
    free(new_ui_buffer->filename);
    new_ui_buffer->filename = strdup(filename);
  }
  zsvsheet_ui_buffer_push(ui_buffer_stack_bottom, ui_buffer_stack_top, new_ui_buffer);
  return 0;
}

/****** API ******/

/**
 * Open a tabular file as a new buffer
 */
zsvsheet_status zsvsheet_open_file(struct zsvsheet_proc_context *ctx, const char *filepath, struct zsv_opts *zopts) {
  struct zsvsheet_builtin_proc_state *state = (struct zsvsheet_builtin_proc_state *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  if (!di || !di->ui_buffers.base || !di->ui_buffers.current)
    return zsvsheet_status_error;
  int err =
    zsvsheet_ui_buffer_open_file(filepath, zopts, NULL, NULL, NULL, di->ui_buffers.base, di->ui_buffers.current);
  if (err)
    return zsvsheet_status_error;
  return zsvsheet_status_ok;
}
