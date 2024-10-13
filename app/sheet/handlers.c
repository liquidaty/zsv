enum zsvsheet_status zsvsheet_file_subcommand_handler(int ch, struct zsvsheet_ui_buffer *ui_buffers,
                                                      struct zsvsheet_ui_buffer *current_ui_buffer, char *buff,
                                                      size_t buff_sz) {
  (void)(ui_buffers);
  (void)(current_ui_buffer);
  int zsvsheetch = zsvsheet_key_binding(ch);
  int n;
  if (zsvsheetch == zsvsheet_key_filter && (n = snprintf(buff, buff_sz, "Filter")) > 0 && (size_t)n < buff_sz)
    return zsvsheet_status_ok;
  if (zsvsheetch == zsvsheet_key_open_file && (n = snprintf(buff, buff_sz, "File to open")) > 0 && (size_t)n < buff_sz)
    return zsvsheet_status_ok;
  snprintf(buff, buff_sz, "Error! Unrecognized file command");
  return zsvsheet_status_error;
}

enum zsvsheet_status zsvsheet_file_handler(struct zsvsheet_handler_context *ctx) {
  struct zsvsheet_ui_buffer *current_ui_buffer = *ctx->ui_buffers.current;
  int zsvsheetch = zsvsheet_key_binding(ctx->ch);
  int err;
  if ((err = zsvsheet_ui_buffer_open_file(
         zsvsheetch == zsvsheet_key_filter ? current_ui_buffer->filename : ctx->subcommand_value,
         zsvsheetch == zsvsheet_key_filter ? &current_ui_buffer->zsv_opts : NULL,
         zsvsheetch == zsvsheet_key_filter ? ctx->subcommand_value : NULL, ctx->custom_prop_handler, ctx->opts_used,
         ctx->ui_buffers.base, ctx->ui_buffers.current))) {
    if (err > 0)
      zsvsheet_set_status(ctx->display_dims, 1, "%s: %s", current_ui_buffer->filename, strerror(err));
    else if (err < 0)
      zsvsheet_set_status(ctx->display_dims, 1, "Unexpected error");
    else
      zsvsheet_set_status(ctx->display_dims, 1, "Not found: %s", ctx->subcommand_value);
    return zsvsheet_status_continue;
  }
  if (ctx->ch == zsvsheet_key_filter && current_ui_buffer->dimensions.row_count < 2) {
    zsvsheet_ui_buffer_pop(ctx->ui_buffers.base, ctx->ui_buffers.current, NULL);
    zsvsheet_set_status(ctx->display_dims, 1, "Not found: %s", ctx->subcommand_value);
  }
  return zsvsheet_status_ok;
}
