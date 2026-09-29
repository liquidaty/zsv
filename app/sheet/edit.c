#include <stdarg.h>
#include <sys/stat.h>
#include <zsv/utils/os.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/writer.h>
#include "../utils/index.h"

#include "edits.h"

// Size of the edit prompt's buffer: a value may be one byte shorter
#define ZSVSHEET_EDIT_PROMPT_SIZE 4096

// Suffix zsv_mkstemp() replaces to name the temp file a save writes beside its target
#define ZSVSHEET_SAVE_TEMP_SUFFIX ".XXXXXX"

// Temp-file prefix for data files that hold committed edits (at most 3 chars on Windows)
#define ZSVSHEET_COMMIT_TEMP_PREFIX "zse"

// Writer hooks that index the rows as they are written, as transformation.c does
struct zsvsheet_export_index {
  struct zsv_index *ix;
  zsv_csv_writer writer;
  char failed;
};

static void zsvsheet_export_index_row(void *p) {
  struct zsvsheet_export_index *xi = p;
  if (zsv_index_add_row(xi->ix, zsv_writer_cum_bytes_written(xi->writer)) != zsv_index_status_ok)
    xi->failed = 1;
}

static void zsvsheet_export_index_end(void *p) {
  struct zsvsheet_export_index *xi = p;
  uint64_t written = zsv_writer_cum_bytes_written(xi->writer);
  if (written && zsv_index_add_row(xi->ix, written) != zsv_index_status_ok)
    xi->failed = 1;
  zsv_index_commit_rows(xi->ix);
}

// Parser warnings that mean cells were dropped (columns beyond max_columns, or a row
// longer than max_row_size); the parser reports these only as messages, so an export
// recognizes them by their format strings and stops rather than lose data
static const char *const zsvsheet_export_lossy_warnings[] = {"exceeds row max", "truncated"};

struct zsvsheet_export_warning {
  char warned;
  char msg[256];
};

static int zsvsheet_export_warn(void *ctx, const char *fmt, ...) {
  struct zsvsheet_export_warning *w = ctx;
  char lossy = 0;
  for (size_t i = 0; i < sizeof(zsvsheet_export_lossy_warnings) / sizeof(*zsvsheet_export_lossy_warnings); i++)
    lossy = lossy || strstr(fmt, zsvsheet_export_lossy_warnings[i]);
  if (lossy && !w->warned) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(w->msg, sizeof(w->msg), fmt, args);
    va_end(args);
    w->msg[strcspn(w->msg, "\r\n")] = '\0';
    w->warned = 1;
  }
  return 0;
}

// Write the rows of the file-backed uib, with pending edits applied, to w
static int zsvsheet_export_file_rows(struct zsvsheet_ui_buffer *uib, zsv_csv_writer w, char *err, size_t errsz) {
  int rc = -1;
  const char *src = uib->data_filename ? uib->data_filename : uib->filename;
  zsv_parser parser = NULL;
  struct zsvsheet_export_warning warning = {0};
  struct zsv_opts opts = uib->zsv_opts;
  if (!(opts.stream = zsv_fopen(src, "rb"))) {
    snprintf(err, errsz, "%s: %s", src, strerror(errno));
    return -1;
  }
  opts.errprintf = zsvsheet_export_warn;
  opts.errf = &warning;
  if (zsv_new_with_properties(&opts, uib->custom_prop_handler, src, &parser) != zsv_status_ok) {
    snprintf(err, errsz, "unable to read %s", src);
    goto out;
  }

  // rows arrive in order, so one pass over the sorted edits suffices
  const struct zsvsheet_edits *e = &uib->edits;
  size_t k = 0; // next edit to write
  enum zsv_status zst = zsv_status_ok;
  enum zsv_writer_status wstat = zsv_writer_status_ok;
  for (size_t row = 0;
       wstat == zsv_writer_status_ok && !warning.warned && (zst = zsv_next_row(parser)) == zsv_status_row; row++) {
    size_t n = zsv_cell_count(parser);
    size_t row_end = k; // edits [k, row_end) belong to this row
    while (row_end < e->count && e->items[row_end].row == row)
      row_end++;
    size_t cols = n ? n : 1; // a blank line is one empty cell
    if (row_end > k && e->items[row_end - 1].col >= cols)
      cols = e->items[row_end - 1].col + 1;
    for (size_t i = 0; i < cols && wstat == zsv_writer_status_ok; i++) {
      if (k < row_end && e->items[k].col == i) {
        wstat = zsv_writer_cell(w, i == 0, e->items[k].value, e->items[k].len, 1);
        k++;
      } else {
        struct zsv_cell c = i < n ? zsv_get_cell(parser, i) : (struct zsv_cell){0};
        wstat = zsv_writer_cell(w, i == 0, c.str, c.len, c.quoted); // flags commas for any delimiter
      }
    }
  }

  if (warning.warned)
    snprintf(err, errsz, "%s", warning.msg);
  else if (wstat != zsv_writer_status_ok)
    snprintf(err, errsz, "write failed");
  else if (zst != zsv_status_done && zst != zsv_status_no_more_input)
    snprintf(err, errsz, "unable to read %s", src);
  else if (k < e->count) // an edit names a row the file no longer has
    snprintf(err, errsz, "%s has fewer rows than when it was edited", src);
  else
    rc = 0;

out:
  zsv_delete(parser);
  fclose(opts.stream);
  return rc;
}

// Write the cells of uib's screen buffer, which holds the whole of a buffer without a file, to w
static int zsvsheet_export_screen_rows(struct zsvsheet_ui_buffer *uib, zsv_csv_writer w, char *err, size_t errsz) {
  enum zsv_writer_status wstat = zsv_writer_status_ok;
  size_t rows = uib->buff_used_rows, cols = uib->dimensions.col_count;
  for (size_t r = 0; r < rows && wstat == zsv_writer_status_ok; r++)
    for (size_t c = 0; c < cols && wstat == zsv_writer_status_ok; c++) {
      const unsigned char *cell = zsvsheet_screen_buffer_cell_display(uib->buffer, r, c);
      wstat =
        zsv_writer_cell(w, c == 0, cell ? cell : (const unsigned char *)"", cell ? strlen((const char *)cell) : 0, 1);
    }
  if (wstat == zsv_writer_status_ok)
    return 0;
  snprintf(err, errsz, "write failed");
  return -1;
}

// Write uib's contents as CSV to out: its file with pending edits applied, or, for a buffer
// without a file, its screen cells. If ix is non-NULL, index the rows written.
// Returns 0, or -1 with a message in err
static int zsvsheet_ui_buffer_export(struct zsvsheet_ui_buffer *uib, FILE *out, struct zsv_index *ix, char *err,
                                     size_t errsz) {
  struct zsvsheet_buffer_info_internal info = zsvsheet_buffer_info_internal(uib);
  if (info.write_in_progress && !info.write_done) { // its file is still being written
    snprintf(err, errsz, "still loading; try again when it finishes");
    return -1;
  }
  struct zsvsheet_export_index xi = {.ix = ix};
  zsv_csv_writer w = zsv_writer_new(&(struct zsv_csv_writer_options){
    .stream = out,
    .on_row = ix ? zsvsheet_export_index_row : NULL,
    .on_row_ctx = &xi,
    .on_delete = ix ? zsvsheet_export_index_end : NULL,
    .on_delete_ctx = &xi,
  });
  if (!w) {
    snprintf(err, errsz, "out of memory");
    return -1;
  }
  xi.writer = w;
  int rc = zsvsheet_ui_buffer_has_file(uib) ? zsvsheet_export_file_rows(uib, w, err, errsz)
                                            : zsvsheet_export_screen_rows(uib, w, err, errsz);
  if (zsv_writer_delete(w) != zsv_writer_status_ok && !rc) {
    snprintf(err, errsz, "write failed");
    rc = -1;
  }
  if (xi.failed && !rc) {
    snprintf(err, errsz, "out of memory");
    rc = -1;
  }
  return rc;
}

// Make uib read a file that already holds every edit and the rows ix indexes: take
// ownership of new_data_filename (NULL: keep reading the current file, which has been
// rewritten) and of ix. The file is plain CSV, so parse it with default options, except
// for those that decide which rows it has
static void zsvsheet_ui_buffer_adopt(struct zsvsheet_ui_buffer *uib, char *new_data_filename, struct zsv_index *ix) {
  zsvsheet_ui_buffer_stop_worker(uib); // it may be reading the old file or index

  pthread_mutex_lock(&uib->mutex);
  struct zsv_index *old_ix = uib->index;
  uib->index = ix;
  uib->index_started = 1;
  uib->index_ready = 1;
  pthread_mutex_unlock(&uib->mutex);
  zsv_index_delete(old_ix);

  if (new_data_filename) {
    if (uib->data_filename) { // a temp file the buffer owns
      unlink(uib->data_filename);
      free(uib->data_filename);
    }
    uib->data_filename = new_data_filename;
  }
  zsvsheet_edits_clear(&uib->edits);
  uib->zsv_opts = (struct zsv_opts){
    .max_columns = uib->zsv_opts.max_columns,
    .max_row_size = uib->zsv_opts.max_row_size,
    .keep_empty_header_rows = uib->zsv_opts.keep_empty_header_rows, // leading blank rows were written as rows
  };
}

// Write uib's pending edits into a new temp data file that uib then reads, so that
// whatever reads the file sees them. Returns 0 (also when there are none), or -1 with a
// message in err
static int zsvsheet_ui_buffer_commit_edits(struct zsvsheet_ui_buffer *uib, char *err, size_t errsz) {
  if (!uib->edits.count)
    return 0;
  int rc = -1;
  FILE *f = NULL;
  struct zsv_index *ix = NULL;
  char *tmp = zsv_get_temp_filename(ZSVSHEET_COMMIT_TEMP_PREFIX);
  if (!tmp || !(ix = zsv_index_new()) || !(f = zsv_fopen(tmp, "wb"))) {
    snprintf(err, errsz, "unable to create a temporary file: %s", strerror(errno));
    goto out;
  }
  rc = zsvsheet_ui_buffer_export(uib, f, ix, err, errsz);
  if (fclose(f) && !rc) {
    snprintf(err, errsz, "write failed: %s", strerror(errno));
    rc = -1;
  }
  if (!rc) {
    zsvsheet_ui_buffer_adopt(uib, tmp, ix);
    return 0;
  }
out:
  zsv_index_delete(ix);
  if (tmp) {
    zsv_remove(tmp);
    free(tmp);
  }
  return rc;
}

// Save uib as CSV to path, the file target names. Rows are written to a temp file beside
// it that then replaces it, so a failure leaves it as it was. Returns 0, or -1 with the
// reason, which names target, in err
static int zsvsheet_ui_buffer_save_to(struct zsvsheet_ui_buffer *uib, const char *target, const char *path, char *err,
                                      size_t errsz) {
  // refuse what would not read back as it reads now, under either name (properties and
  // the implied delimiter follow the name, and a link's name can differ from its file's)
  const char *names[] = {target, path};
  for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) { // messages name what the user typed
    struct zsv_opts name_opts = {0};
    if (zsv_opts_load_properties(&name_opts, uib->custom_prop_handler, names[i]) != zsv_status_ok) {
      if (i)
        snprintf(err, errsz, "unable to load the properties of the file %s links to", target);
      else
        snprintf(err, errsz, "unable to load the properties of %s", target);
      return -1;
    }
    if (!zsvsheet_opts_plain_csv(&name_opts)) {
      if (i)
        snprintf(err, errsz, "the file %s links to would not read back as CSV", target);
      else
        snprintf(err, errsz, "%s would not read back as CSV", target);
      return -1;
    }
  }
  for (struct zsvsheet_ui_buffer *b = uib->prior; b; b = b->prior) {
    int same = b->filename && !b->data_filename ? zsv_same_file(b->filename, path) : 0;
    if (same) { // -1 (cannot tell) is refused too
      snprintf(err, errsz,
               same > 0 ? "%s is open in another view" : "unable to tell whether %s is open in another view", target);
      return -1;
    }
  }
  struct stat path_st;
  if (!stat(path, &path_st) && S_ISDIR(path_st.st_mode)) {
    snprintf(err, errsz, "%s is a directory", target);
    return -1;
  }
  if (zsv_file_exists(path) && access(path, W_OK)) { // replacing it would bypass its permissions
    snprintf(err, errsz, "%s is read-only", target);
    return -1;
  }
  const int saves_source = uib->filename ? zsv_same_file(uib->filename, path) : 0;
  if (saves_source < 0) {
    snprintf(err, errsz, "unable to tell whether %s is the file this view was opened from", target);
    return -1;
  }
  if (saves_source && !uib->filename_plain_csv) { // even after a commit, the file itself is what changes
    snprintf(err, errsz, "rewriting %s as CSV would change it; save to a new .csv file", target);
    return -1;
  }
  // uib reads path, whose index must follow. Predicted before the replace; confirmed after it
  const char in_place = saves_source && !uib->data_filename;

  int rc = -1;
  FILE *f = NULL;
  struct zsv_index *ix = NULL;
  char created_path = 0; // path was created here, empty, and is removed if the save fails
  size_t tmp_len = strlen(path) + sizeof(ZSVSHEET_SAVE_TEMP_SUFFIX);
  char *tmp = malloc(tmp_len);
  if (!tmp) {
    snprintf(err, errsz, "out of memory");
    return -1;
  }
  snprintf(tmp, tmp_len, "%s%s", path, ZSVSHEET_SAVE_TEMP_SUFFIX);
  int fd = zsv_mkstemp(tmp);
  if (fd == -1) {
    snprintf(err, errsz, "%s: %s", target, strerror(errno));
    free(tmp);
    return -1;
  }
  if (!(f = fdopen(fd, "wb"))) {
    snprintf(err, errsz, "%s: %s", target, strerror(errno));
    close(fd);
    goto out;
  }
  if (in_place && !(ix = zsv_index_new())) {
    snprintf(err, errsz, "out of memory");
    goto out;
  }
  if (zsvsheet_ui_buffer_export(uib, f, ix, err, errsz))
    goto out;
  int close_failed = fflush(f) || zsv_fsync(fileno(f));
  close_failed = fclose(f) || close_failed;
  f = NULL;
  if (close_failed) {
    snprintf(err, errsz, "%s: %s", target, strerror(errno));
    goto out;
  }
  // a new file gets the permissions the system gives one: create it, then copy its permissions
  if (!zsv_file_exists(path)) {
    int create_err = zsv_create_new_file(path);
    if (create_err) {
      snprintf(err, errsz, "%s: %s", target, strerror(create_err));
      goto out;
    }
    created_path = 1;
  }
  int perm_err = zsv_copy_permissions(path, tmp);
  if (perm_err) {
    snprintf(err, errsz, "unable to set permissions: %s", strerror(perm_err));
    goto out;
  }
  if (in_place)
    zsvsheet_ui_buffer_stop_worker(uib); // Windows cannot replace a file the index worker holds open
  int replace_err = zsv_replace_file(tmp, path);
  if (replace_err) {
    if (in_place) // a stopped index worker leaves a partial index
      zsvsheet_ui_buffer_reset_index(uib);
    snprintf(err, errsz, "unable to replace %s: %s", target, strerror(replace_err));
    goto out;
  }
  free(tmp);
  tmp = NULL;
  rc = 0;

  // the replace gave path a new file. A hard link to the opened file is another name for
  // it, so the opened name may still name the old file, unchanged and without the edits
  const char saved_source = saves_source && zsv_same_file(uib->filename, path) == 1;
  if (in_place && saved_source) { // reuse the index built while writing, rather than index the file again
    zsvsheet_ui_buffer_adopt(uib, NULL, ix);
    ix = NULL;
  } else if (in_place) // the stopped index worker may have left a partial index of the old file
    zsvsheet_ui_buffer_reset_index(uib);
  if (saved_source || !uib->filename) {
    uib->modified = 0;
    if (!uib->filename && zsvsheet_ui_buffer_has_file(uib)) // name the buffer after the file it was saved as
      uib->filename = strdup(target); // as typed: each :w resolves it again. On failure the next :w asks again
  }

out:
  if (f)
    fclose(f);
  if (tmp) {
    zsv_remove(tmp);
    free(tmp);
  }
  if (rc && created_path)
    zsv_remove(path);
  zsv_index_delete(ix);
  return rc;
}

// Save uib as CSV to target, or to the file it links to. Returns 0, or -1 with the reason in err
static int zsvsheet_ui_buffer_save(struct zsvsheet_ui_buffer *uib, const char *target, char *err, size_t errsz) {
  char *path = zsv_final_path(target);
  if (!path) {
    if (errno == ENOENT) // the one case in which zsv_final_path() reports ENOENT
      snprintf(err, errsz, "%s links to a file that does not exist", target);
    else
      snprintf(err, errsz, "%s: %s", target, strerror(errno));
    return -1;
  }
  int rc = zsvsheet_ui_buffer_save_to(uib, target, path, err, errsz);
  free(path);
  return rc;
}

static zsvsheet_status zsvsheet_edit_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *uib = *di->ui_buffers.current;

  // buffer row 0 is the header (raw row 0); buffer row b >= 1 is raw row input_offset.row + b
  const size_t buff_row = uib->cursor_row ? uib->buff_offset.row + uib->cursor_row : 0;
  const size_t raw_row = buff_row ? uib->input_offset.row + buff_row : 0;
  const size_t screen_col = uib->buff_offset.col + uib->cursor_col;
  // row numbers (sheet's own column, or the "Row #" a derived view's file carries) are not data
  const size_t col_offset = zsvsheet_ui_buffer_data_col_offset(uib);
  if (screen_col < col_offset + uib->has_row_num) {
    zsvsheet_ui_buffer_set_status(uib, "Row numbers cannot be edited");
    return zsvsheet_status_ok;
  }
  const size_t col = screen_col - col_offset;
  if (buff_row >= uib->buff_used_rows || col >= uib->dimensions.col_count)
    return zsvsheet_status_ok; // no cell under the cursor, e.g. the blank start screen

  char prompt_buffer[ZSVSHEET_EDIT_PROMPT_SIZE];
  const char *current = (const char *)zsvsheet_screen_buffer_cell_display(uib->buffer, buff_row, screen_col);
  const char *value;
  if (ctx->num_params > 1) {
    zsvsheet_ui_buffer_set_status(uib, "Quote a value that contains spaces, e.g. :cell \"a b\"");
    return zsvsheet_status_ok;
  } else if (ctx->num_params == 1)
    value = ctx->params[0].u.string;
  else if (!ctx->invocation.interactive)
    return zsvsheet_status_error;
  else {
    const char *prompt = "Edit";
    // the prompt edits a single screen line: ": " follows the prompt text
    if (current && strlen(prompt) + 2 + strlen(current) + 1 >= di->dimensions->columns) {
      zsvsheet_ui_buffer_set_status(uib, "Value too long to edit here; use :cell \"<new value>\"");
      return zsvsheet_status_ok;
    }
    int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
    if (!get_subcommand(prompt, prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, current, 0))
      return zsvsheet_status_ok; // cancelled
    value = prompt_buffer;
  }

  size_t len = strlen(value);
  if (!strcmp(value, current ? current : "")) // nothing to record, and nothing to commit later
    return zsvsheet_status_ok;
  if (zsvsheet_ui_buffer_has_file(uib) &&
      zsvsheet_edits_set(&uib->edits, raw_row, col, (const unsigned char *)value, len)) {
    zsvsheet_ui_buffer_set_status(uib, "Out of memory");
    return zsvsheet_status_ok;
  }
  enum zsvsheet_priv_status wstat =
    zsvsheet_screen_buffer_write_cell_w_len(uib->buffer, buff_row, screen_col, (const unsigned char *)value, len);
  if (wstat != zsvsheet_priv_status_ok && !zsvsheet_ui_buffer_has_file(uib)) { // the screen buffer is the only copy
    zsvsheet_ui_buffer_set_status(uib, "Out of memory");
    return zsvsheet_status_ok;
  }
  if (wstat != zsvsheet_priv_status_ok && buff_row) // a reload shows the recorded edit (and leaves the header row)
    di->update_buffer = 1;
  uib->modified = 1;
  zsvsheet_ui_buffer_set_status(uib, wstat == zsvsheet_priv_status_ok || buff_row
                                       ? "Modified; :w to save"
                                       : "Out of memory: the edit is kept but not shown");
  if (state->compare.active)
    zsvsheet_apply_compare_attrs(uib, &state->compare);
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_write_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *uib = *di->ui_buffers.current;

  char prompt_buffer[256] = {0};
  const char *target;
  if (ctx->num_params > 1) {
    zsvsheet_ui_buffer_set_status(uib, "Quote a file name that contains spaces");
    return zsvsheet_status_ok;
  } else if (ctx->num_params == 1)
    target = ctx->params[0].u.string;
  else if (uib->filename)
    target = uib->filename;
  else if (!ctx->invocation.interactive)
    return zsvsheet_status_error;
  else {
    int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
    get_subcommand("Save as", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL, 0);
    if (!*prompt_buffer)
      return zsvsheet_status_ok;
    target = prompt_buffer;
  }

  char err[256];
  if (zsvsheet_ui_buffer_save(uib, target, err, sizeof(err)))
    zsvsheet_ui_buffer_set_statusf(uib, "Not saved: %s", err);
  else if (uib->modified) // saved elsewhere, e.g. :w other.csv, or to another name (hard link) of the file
    zsvsheet_ui_buffer_set_statusf(uib, "Saved %s; %s still has unsaved changes", target, uib->filename);
  else
    zsvsheet_ui_buffer_set_statusf(uib, "Saved %s", target);
  return zsvsheet_status_ok;
}
