enum zsvsheet_status zsvsheet_subcommand_set_prompt(zsvsheet_subcommand_handler_context_t ctx, const char *s) {
  if (strlen(s) < sizeof(ctx->prompt)) {
    memcpy(ctx->prompt, s, strlen(s));
    return zsvsheet_status_ok;
  }
  return zsvsheet_status_error;
}

static int zsvsheet_subcommand_ctx_ch(zsvsheet_subcommand_handler_context_t ctx) {
  return ctx->ch;
}

enum zsvsheet_status zsvsheet_file_subcommand_handler(zsvsheet_subcommand_handler_context_t ctx) {
  int ch = zsvsheet_subcommand_ctx_ch(ctx);
  int zsvsheetch = zsvsheet_key_binding(ch);
  if (zsvsheetch == zsvsheet_key_filter && zsvsheet_subcommand_set_prompt(ctx, "Filter") == zsvsheet_status_ok)
    return zsvsheet_status_ok;
  if (zsvsheetch == zsvsheet_key_open_file && zsvsheet_subcommand_set_prompt(ctx, "File to open") == zsvsheet_status_ok)
    return zsvsheet_status_ok;
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

struct zsvsheet_key_handler_data *zsvsheet_get_registered_key_handler(int ch, struct zsvsheet_key_handler_data *root) {
  for (struct zsvsheet_key_handler_data *kh = root; kh; kh = kh->next)
    if (ch == kh->ch)
      return kh;
  return NULL;
}

extern struct zsvsheet_key_handler_data *zsvsheet_key_handlers, **zsvsheet_next_key_handler;

enum zsvsheet_status zsvsheet_register_key_handler(
  int ch, enum zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  enum zsvsheet_status (*handler)(zsvsheet_handler_context_t)) {
  struct zsvsheet_key_handler_data *already_registered = zsvsheet_get_registered_key_handler(ch, zsvsheet_key_handlers);
  if (already_registered)
    return zsvsheet_status_duplicate;
  struct zsvsheet_key_handler_data *new_key_handler = calloc(1, sizeof(*new_key_handler));
  if (!new_key_handler)
    return zsvsheet_status_memory;
  new_key_handler->ch = ch;
  new_key_handler->subcommand_handler = subcommand_handler;
  new_key_handler->handler = handler;
  *zsvsheet_next_key_handler = new_key_handler;
  zsvsheet_next_key_handler = &new_key_handler->next;
  return zsvsheet_status_ok;
}

enum zsvsheet_status zsvsheet_register_key_handler_internal(
  enum zsvsheet_key zch, enum zsvsheet_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  enum zsvsheet_status (*handler)(zsvsheet_handler_context_t)) {
  int ch = zsvsheet_ch_from_zsvsheetch(zch);
  assert(ch != -1);
  if (ch == -1)
    return zsvsheet_status_error;
  return zsvsheet_register_key_handler(ch, subcommand_handler, handler);
}
