#include "file.h"

static void zsvsheet_key_handlers_delete(struct zsvsheet_key_handler_data **root,
                                         struct zsvsheet_key_handler_data ***nextp) {
  for (struct zsvsheet_key_handler_data *next, *e = *root; e; e = next) {
    next = e->next;
    free(e->long_name);
    free(e);
  }
  *root = NULL;
  *nextp = &(*root)->next;
}

zsvsheet_handler_status zsvsheet_file_subcommand_handler(zsvsheet_subcommand_handler_context_t ctx) {
  int ch = zsvsheet_handler_key(ctx);
  int zsvsheetch = zsvsheet_key_binding(ch);
  if (zsvsheetch == zsvsheet_key_filter && zsvsheet_subcommand_prompt(ctx, "Filter") == zsvsheet_handler_status_ok)
    return zsvsheet_handler_status_ok;
  if (zsvsheetch == zsvsheet_key_open_file &&
      zsvsheet_subcommand_prompt(ctx, "File to open") == zsvsheet_handler_status_ok)
    return zsvsheet_handler_status_ok;
  return zsvsheet_handler_status_error;
}

zsvsheet_handler_status zsvsheet_file_handler(struct zsvsheet_handler_context *ctx) {
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
    return zsvsheet_handler_status_ignore;
  }
  if (ctx->ch == zsvsheet_key_filter && current_ui_buffer->dimensions.row_count < 2) {
    zsvsheet_ui_buffer_pop(ctx->ui_buffers.base, ctx->ui_buffers.current, NULL);
    zsvsheet_set_status(ctx->display_dims, 1, "Not found: %s", ctx->subcommand_value);
  }
  return zsvsheet_handler_status_ok;
}

struct zsvsheet_key_handler_data *zsvsheet_get_registered_key_handler(int ch, const char *long_name,
                                                                      struct zsvsheet_key_handler_data *root) {
  for (struct zsvsheet_key_handler_data *kh = root; kh; kh = kh->next)
    if (ch == kh->ch || (long_name && kh->long_name && !strcmp(long_name, kh->long_name)))
      return kh;
  return NULL;
}

extern struct zsvsheet_key_handler_data *zsvsheet_key_handlers, **zsvsheet_next_key_handler;

zsvsheet_handler_status zsvsheet_register_command(
  int ch, const char *long_name, zsvsheet_handler_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  zsvsheet_handler_status (*handler)(zsvsheet_handler_context_t)) {
  struct zsvsheet_key_handler_data *already_registered =
    zsvsheet_get_registered_key_handler(ch, long_name, zsvsheet_key_handlers);
  if (already_registered)
    return zsvsheet_handler_status_duplicate;
  struct zsvsheet_key_handler_data *new_key_handler = calloc(1, sizeof(*new_key_handler));
  if (!new_key_handler)
    return zsvsheet_handler_status_memory;
  new_key_handler->ch = ch;
  new_key_handler->long_name = strdup(long_name);
  new_key_handler->subcommand_handler = subcommand_handler;
  new_key_handler->handler = handler;
  *zsvsheet_next_key_handler = new_key_handler;
  zsvsheet_next_key_handler = &new_key_handler->next;
  return zsvsheet_handler_status_ok;
}

zsvsheet_handler_status zsvsheet_register_command_internal(
  enum zsvsheet_key zch, const char *long_name,
  zsvsheet_handler_status (*subcommand_handler)(zsvsheet_subcommand_handler_context_t),
  zsvsheet_handler_status (*handler)(zsvsheet_handler_context_t)) {
  int ch = zsvsheet_ch_from_zsvsheetch(zch);
  assert(ch != -1);
  if (ch == -1)
    return zsvsheet_handler_status_error;
  return zsvsheet_register_command(ch, long_name, subcommand_handler, handler);
}

/****** API ******/

/**
 * Set the subcommand prompt
 */
zsvsheet_handler_status zsvsheet_subcommand_prompt(zsvsheet_subcommand_handler_context_t ctx, const char *fmt, ...) {
  va_list argv;
  va_start(argv, fmt);
  int n = vsnprintf(ctx->prompt, sizeof(ctx->prompt), fmt, argv);
  va_end(argv);
  if (n > 0 && (size_t)n < sizeof(ctx->prompt))
    return zsvsheet_handler_status_ok;
  return zsvsheet_handler_status_error;
}

/**
 * Set a status message
 */
zsvsheet_handler_status zsvsheet_handler_set_status(zsvsheet_handler_context_t ctx, const char *fmt, ...) {
  va_list argv;
  va_start(argv, fmt);
  vsnprintf(zsvsheet_status_text, sizeof(zsvsheet_status_text), fmt, argv);
  va_end(argv);
  // note: if (n < (int)sizeof(zsvsheet_status_text)), then we just ignore
  zsvsheet_display_status_text(ctx->display_dims);
  return zsvsheet_handler_status_ok;
}

/**
 * Get the key press that triggered this subcommand handler
 */
int zsvsheet_handler_key(zsvsheet_subcommand_handler_context_t ctx) {
  return ctx->ch;
}

/**
 * Get the current buffer
 */
zsvsheet_handler_buffer_t zsvsheet_handler_buffer_current(zsvsheet_handler_context_t h) {
  struct zsvsheet_handler_context *ctx = h;
  return ctx && ctx->ui_buffers.current ? *ctx->ui_buffers.current : NULL;
}

/**
 * Get the prior buffer
 */
zsvsheet_handler_buffer_t zsvsheet_handler_buffer_prior(zsvsheet_handler_buffer_t b) {
  struct zsvsheet_ui_buffer *uib = b;
  return uib ? uib->prior : NULL;
}

/**
 * Get the filename associated with a buffer
 */
const char *zsvsheet_handler_buffer_filename(zsvsheet_handler_buffer_t h) {
  struct zsvsheet_ui_buffer *uib = h;
  return uib ? uib->filename : NULL;
}
