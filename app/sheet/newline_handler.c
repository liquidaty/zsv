static zsvsheet_status zsvsheet_newline_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_ui_buffer *current_ui_buffer = *state->display_info.ui_buffers.current;
  if (current_ui_buffer->on_newline)
    current_ui_buffer->on_newline(ctx);
  return zsvsheet_status_ok;
}
