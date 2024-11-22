#include "file.h"
#include "procedure.h"

static void zsvsheet_key_handlers_delete(struct zsvsheet_key_data **root, struct zsvsheet_key_data ***nextp) {
  for (struct zsvsheet_key_data *next, *e = *root; e; e = next) {
    next = e->next;
    free(e->long_name);
    free(e);
  }
  *root = NULL;
  *nextp = &(*root)->next;
}

struct zsvsheet_key_data *zsvsheet_get_registered_key_handler(int ch, const char *long_name,
                                                              struct zsvsheet_key_data *root) {
  for (struct zsvsheet_key_data *kh = root; kh; kh = kh->next)
    if (ch == kh->ch || (long_name && kh->long_name && !strcmp(long_name, kh->long_name)))
      return kh;
  return NULL;
}

/****** API ******/

/**
 * Set the subcommand prompt
 */
zsvsheet_status zsvsheet_subcommand_prompt(zsvsheet_subcommand_context_t ctx, const char *fmt, ...) {
  va_list argv;
  va_start(argv, fmt);
  int n = vsnprintf(ctx->prompt, sizeof(ctx->prompt), fmt, argv);
  va_end(argv);
  if (n > 0 && (size_t)n < sizeof(ctx->prompt))
    return zsvsheet_status_ok;
  return zsvsheet_status_error;
}

/**
 * Set a status message
 */
zsvsheet_status zsvsheet_set_status(struct zsvsheet_proc_context *ctx, const char *fmt, ...) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  va_list argv;
  va_start(argv, fmt);
  vsnprintf(zsvsheet_status_text, sizeof(zsvsheet_status_text), fmt, argv);
  va_end(argv);
  // note: if (n < (int)sizeof(zsvsheet_status_text)), then we just ignore
  zsvsheet_display_status_text(state->display_info.dimensions);
  return zsvsheet_status_ok;
}

/**
 * Get the key press that triggered this subcommand handler
 */
int zsvsheet_ext_keypress(zsvsheet_proc_context_t ctx) {
  if (ctx && ctx->invocation.type == zsvsheet_proc_invocation_type_keypress)
    return ctx->invocation.u.keypress.ch;
  return -1;
}

/**
 * Get the current buffer
 */
zsvsheet_buffer_t zsvsheet_buffer_current(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  return state && state->display_info.ui_buffers.current ? *state->display_info.ui_buffers.current : NULL;
}

/**
 * Get the prior buffer
 */
zsvsheet_buffer_t zsvsheet_buffer_prior(zsvsheet_buffer_t b) {
  struct zsvsheet_ui_buffer *uib = b;
  return uib ? uib->prior : NULL;
}

/**
 * Get the filename associated with a buffer
 */
const char *zsvsheet_buffer_filename(zsvsheet_buffer_t h) {
  struct zsvsheet_ui_buffer *uib = h;
  return uib ? uib->filename : NULL;
}

/**
 * Get the data file associated with a buffer. This might not be the same as the filename,
 * such as when the data has been filtered
 */
const char *zsvsheet_buffer_data_filename(zsvsheet_buffer_t h) {
  struct zsvsheet_ui_buffer *uib = h;
  if (uib)
    return uib->data_filename ? uib->data_filename : uib->filename;
  return NULL;
}

/**
 * Set custom context
 * @param on_close optional callback to invoke when the buffer is closed
 *
 * @return zsv_ext_status_ok on success, else zsv_ext_status error code
 */
enum zsv_ext_status zsvsheet_buffer_set_ctx(zsvsheet_buffer_t h, void *ctx, void (*on_close)(void *)) {
  if (h) {
    // TO DO: return zsv_ext_status_not_permitted if this buffer is protected and the caller is not authorized
    struct zsvsheet_ui_buffer *uib = h;
    uib->ext_ctx = ctx;
    uib->ext_on_close = on_close;
  }
  return zsv_ext_status_ok;
}

/**
 * Get custom context previously set via zsvsheet_buffer_set_ctx()
 */
enum zsv_ext_status zsvsheet_buffer_get_ctx(zsvsheet_buffer_t h, void **ctx_out) {
  // TO DO: return zsv_ext_status_not_permitted if this buffer is protected and the caller is not authorized
  *ctx_out = h ? ((struct zsvsheet_ui_buffer *)h)->ext_ctx : NULL;
  return zsv_ext_status_ok;
}

/** Get zsv_opts use to open the buffer's data file **/
struct zsv_opts zsvsheet_buffer_get_zsv_opts(zsvsheet_buffer_t h) {
  if (h) {
    struct zsvsheet_ui_buffer *buff = h;
    return buff->zsv_opts;
  }
  struct zsv_opts opts = {0};
  return opts;
}
