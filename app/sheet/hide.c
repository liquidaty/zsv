/*
 * The :hide-column and :unhide-column commands. Hiding is a view setting of the current
 * buffer (see hidden-columns.h): the grid, the cursor and the column searches skip the
 * hidden columns, while the data, the saves and the transformations keep every column.
 *
 * Included from sheet.c among the other command modules: it sees the sheet context, the
 * ui_buffer hidden-column accessors, and the geometry helpers above (zsvsheet_max_buffer_cols(),
 * zsvsheet_settle_view_cols()).
 */

// The header text of a screen-buffer column, "" when the buffer has no such column
static const char *zsvsheet_col_name(const struct zsvsheet_ui_buffer *uib, size_t col) {
  const unsigned char *name = zsvsheet_screen_buffer_cell_display(uib->buffer, 0, col);
  return name ? (const char *)name : "";
}

// Whether an argument names the row-number column (see zsvsheet_ui_buffer_rownum_cols()), which
// is not data and cannot be hidden
static int zsvsheet_colname_is_rownum(const struct zsvsheet_ui_buffer *uib, const char *pattern) {
  return zsvsheet_ui_buffer_rownum_cols(uib) && zsvsheet_column_name_matches(pattern, zsvsheet_col_name(uib, 0));
}

// Data columns the grid shows: the row-number column does not count, as it cannot be hidden
static size_t zsvsheet_visible_data_cols(struct zsvsheet_ui_buffer *uib) {
  return zsvsheet_ui_buffer_visible_col_count(uib, zsvsheet_ui_buffer_rownum_cols(uib), zsvsheet_max_buffer_cols(uib));
}

// What one command did and what it should say about it
struct zsvsheet_hide_scan {
  size_t changed;        // columns hidden or unhidden by this command
  const char *first_bad; // first argument that addressed no column
  char rownum_named;     // an argument named the row-number column, which is not data
  char oom;              // a hide ran out of memory; the status says so, and what changed before
};

// Apply one argument to the buffer's data columns: hide them, or unhide the hidden ones
// among them
static void zsvsheet_hide_scan_arg(struct zsvsheet_ui_buffer *uib, const char *pattern, char hide,
                                   struct zsvsheet_hide_scan *scan) {
  const size_t col_offset = zsvsheet_ui_buffer_rownum_cols(uib);
  const size_t max_cols = zsvsheet_max_buffer_cols(uib);
  char addressed = 0;
  for (size_t col = col_offset; col < max_cols; col++) {
    if (!zsvsheet_column_name_matches(pattern, zsvsheet_col_name(uib, col)))
      continue;
    if (hide) {
      addressed = 1; // the argument names this column, whether or not it is hidden already
      if (zsvsheet_ui_buffer_col_hidden(uib, col))
        continue; // already hidden: nothing to do
      if (zsvsheet_ui_buffer_hide_col(uib, col)) {
        scan->oom = 1;
        return;
      }
      scan->changed++;
    } else {
      if (!zsvsheet_ui_buffer_col_hidden(uib, col))
        continue; // not hidden: unhiding has nothing to do with it
      addressed = 1;
      zsvsheet_ui_buffer_unhide_col(uib, col);
      scan->changed++;
    }
  }
  if (!addressed) {
    if (hide && !scan->rownum_named && zsvsheet_colname_is_rownum(uib, pattern))
      scan->rownum_named = 1;
    else if (!scan->first_bad)
      scan->first_bad = pattern;
  }
}

// How many data columns the grid would still show if every column matching one of the
// command's arguments were hidden. 0 would leave no data on screen, which hiding refuses
static size_t zsvsheet_visible_after_hide(struct zsvsheet_ui_buffer *uib, struct zsvsheet_proc_context *ctx) {
  const size_t col_offset = zsvsheet_ui_buffer_rownum_cols(uib);
  const size_t max_cols = zsvsheet_max_buffer_cols(uib);
  size_t remain = zsvsheet_visible_data_cols(uib);
  for (size_t col = col_offset; col < max_cols; col++) {
    char matched = 0;
    // a column that is already hidden neither remains nor is hidden by this command, so it
    // takes no part in either count
    if (zsvsheet_ui_buffer_col_hidden(uib, col))
      continue;
    for (int i = 0; i < ctx->num_params && !matched; i++)
      matched = zsvsheet_column_name_matches(ctx->params[i].u.string, zsvsheet_col_name(uib, col));
    if (matched)
      remain--;
  }
  return remain;
}

static zsvsheet_status zsvsheet_hide_columns_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_dimensions *ddims = state->display_info.dimensions;
  struct zsvsheet_ui_buffer *uib = *state->display_info.ui_buffers.current;
  const char hide = ctx->proc_id == zsvsheet_builtin_proc_hide_column;
  struct zsvsheet_hide_scan scan = {0};
  char msg[256] = {0};

  if (!zsvsheet_ui_buffer_has_file(uib)) {
    zsvsheet_ui_buffer_set_statusf(uib, "%s only available for tabular data buffers", hide ? "Hide" : "Unhide");
    return zsvsheet_status_ok;
  }

  if (!ctx->num_params) {
    if (hide) {
      const size_t col = uib->buff_offset.col + uib->cursor_col; // the cursor's column
      if (col < zsvsheet_ui_buffer_rownum_cols(uib)) {
        zsvsheet_ui_buffer_set_status(uib, "Cannot hide the row-number column");
        return zsvsheet_status_ok;
      }
      if (zsvsheet_visible_data_cols(uib) <= 1) {
        zsvsheet_ui_buffer_set_status(uib, "Cannot hide every column");
        return zsvsheet_status_ok;
      }
      if (zsvsheet_ui_buffer_hide_col(uib, col)) {
        zsvsheet_ui_buffer_set_status(uib, "Out of memory");
        return zsvsheet_status_ok;
      }
      scan.changed = 1;
      if (snprintf(msg, sizeof(msg), "Hid column \"%s\"", zsvsheet_col_name(uib, col)) < 0)
        msg[0] = '\0';
    } else {
      const char *names[2] = {NULL, NULL}; // a couple of names is a status line's worth
      size_t named = 0, i;
      char tail[160] = {0};
      scan.changed = zsvsheet_ui_buffer_hidden_count(uib);
      if (!scan.changed) {
        zsvsheet_ui_buffer_set_status(uib, "No hidden columns");
        return zsvsheet_status_ok;
      }
      // the marker shows only a count, so name them while they are few: this is the command a
      // reader runs to learn what is hidden
      for (i = 0; i < scan.changed && named < sizeof(names) / sizeof(*names); i++) {
        size_t col = zsvsheet_ui_buffer_hidden_col(uib, i);
        if (col != (size_t)-1)
          names[named++] = zsvsheet_col_name(uib, col);
      }
      zsvsheet_ui_buffer_unhide_all(uib);
      if (names[0] && scan.changed <= named) // every hidden column is named, not just some
        snprintf(tail, sizeof(tail), ": %s%s%s", names[0], scan.changed == 2 ? ", " : "",
                 scan.changed == 2 && names[1] ? names[1] : "");
      if (snprintf(msg, sizeof(msg), "Unhid %zu column%s%s", scan.changed, scan.changed == 1 ? "" : "s", tail) < 0)
        msg[0] = '\0';
    }
  } else {
    if (hide && !zsvsheet_visible_after_hide(uib, ctx)) {
      zsvsheet_ui_buffer_set_status(uib, "Cannot hide every column");
      return zsvsheet_status_ok;
    }
    for (int i = 0; i < ctx->num_params && !scan.oom; i++)
      zsvsheet_hide_scan_arg(uib, ctx->params[i].u.string, hide, &scan);
    if (scan.changed) {
      size_t len = 0;
      int n = snprintf(msg, sizeof(msg), hide ? "Hid %zu column%s" : "Unhid %zu column%s", scan.changed,
                       scan.changed == 1 ? "" : "s");
      if (n > 0)
        len = (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1;
      if (len < sizeof(msg)) { // a longer message cannot hold the tail
        if (scan.first_bad)
          snprintf(msg + len, sizeof(msg) - len, hide ? "; not found: %s" : "; no hidden column matches: %s",
                   scan.first_bad);
        else if (scan.rownum_named) // the argument that named the row-number column is not a column
          snprintf(msg + len, sizeof(msg) - len, "; cannot hide the row-number column");
      }
    } else if (scan.first_bad)
      snprintf(msg, sizeof(msg), hide ? "Column not found: %s" : "No hidden column matches: %s", scan.first_bad);
    else if (hide && scan.rownum_named)
      snprintf(msg, sizeof(msg), "Cannot hide the row-number column");
    else
      snprintf(msg, sizeof(msg), hide ? "Columns already hidden" : "No hidden columns");
  }

  if (scan.oom) {
    if (!scan.changed)
      snprintf(msg, sizeof(msg), "Out of memory");
    else if (snprintf(msg, sizeof(msg), "Out of memory after hiding %zu column%s", scan.changed,
                      scan.changed == 1 ? "" : "s") < 0)
      msg[0] = '\0';
  }
  if (scan.changed) // the cursor and the window may sit on a column just hidden
    zsvsheet_settle_view_cols(uib, ddims);
  zsvsheet_ui_buffer_set_status(uib, msg);
  return zsvsheet_status_ok;
}
