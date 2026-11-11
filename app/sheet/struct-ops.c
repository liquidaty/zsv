/* Row and column insert and delete: <ctrl>o, then o / O (a row below / above the cursor), c / C
 * (a column right / left of it), d d or d r (delete the row), or d c (delete the column). The
 * same procedures run as :insert-row [above], :delete-row, :insert-column [left], :delete-column */
#define ZSVSHEET_KEY_STRUCT ZSVSHEET_CTRL('o')
#define ZSVSHEET_STRUCT_KEY_ROW 'o'
#define ZSVSHEET_STRUCT_KEY_ROW_BEFORE 'O'
#define ZSVSHEET_STRUCT_KEY_COL 'c'
#define ZSVSHEET_STRUCT_KEY_COL_BEFORE 'C'
#define ZSVSHEET_STRUCT_KEY_DELETE 'd'
#define ZSVSHEET_STRUCT_KEY_DELETE_ROW 'r' // after d, the same as a second d

// defined in sheet.c, after this file
static void zsvsheet_check_buffer_updates(struct zsvsheet_ui_buffer *ub,
                                          struct zsvsheet_display_dimensions *display_dims,
                                          struct zsvsheet_sheet_context *handler_state);

// Why op (whose `at` is not yet set) cannot be done with the cursor at pos, or NULL if it can
static const char *zsvsheet_struct_op_refusal(const struct zsvsheet_sheet_context *state,
                                              const struct zsvsheet_ui_buffer *uib, const struct zsvsheet_cell_pos *pos,
                                              const struct zsvsheet_struct_op *op) {
  // a view without a file of its own (help, a filter, sort, pivot or :sql result), or one whose
  // rows a "Row #" column or an extension ties to their positions
  if (!uib->filename || uib->has_row_num || uib->ext_ctx || uib->get_cell_attrs || uib->on_newline)
    return "Rows and columns can be inserted or deleted only in a file you opened";
  if (op->axis == zsvsheet_struct_axis_row)
    return op->kind == zsvsheet_struct_op_delete && uib->dimensions.row_count <= 1 ? "The only row cannot be deleted"
                                                                                   : NULL;
  if (pos->row_num_col)
    return "Row numbers are not a column; move to a data column";
  if (state->compare.active) // it holds column positions
    return "Columns cannot be inserted or deleted while :compare is on";
  if (zsvsheet_cell_data_col(uib, pos) >= uib->dimensions.col_count) // past the last column
    return "No column under the cursor";
  if (op->kind == zsvsheet_struct_op_insert && uib->dimensions.col_count >= zsvsheet_ui_buffer_max_cols(uib))
    return "Rows already have the most columns --max-column-count allows";
  return NULL;
}

// Put the cursor where op leaves the user: on an inserted row's first cell, or an inserted
// column; after a delete, on what moved into the deleted row's or column's place
static void zsvsheet_struct_op_place_cursor(struct zsvsheet_display_info *di, struct zsvsheet_ui_buffer *uib,
                                            const struct zsvsheet_struct_op *op) {
  const size_t col_offset = zsvsheet_ui_buffer_data_col_offset(uib);
  const size_t max_cols = zsvsheet_max_buffer_cols(uib);
  const char insert = op->kind == zsvsheet_struct_op_insert;
  if (op->axis == zsvsheet_struct_axis_row) {
    size_t target = zsvsheet_struct_op_pos(op);
    if (target >= uib->dimensions.row_count) // the deleted row was the last
      target = uib->dimensions.row_count - 1;
    if (target)
      zsvsheet_move_ver_to_row(di, target);
    else // the header, always on screen, which zsvsheet_move_ver_to_row() never targets
      uib->cursor_row = 0;
    const size_t first = zsvsheet_ui_buffer_visible_col(uib, col_offset, max_cols);
    if (insert && first < max_cols)
      zsvsheet_move_hor_to(di, first);
    else if (uib->buff_offset.col + uib->cursor_col >= max_cols) {
      // the delete took the cursor's column away: go to the last column left, as if from the first
      uib->buff_offset.col = uib->cursor_col = 0;
      zsvsheet_move_hor_to(di, max_cols);
    }
  } else // after a delete, the first visible column to the right; past the end,
         // zsvsheet_move_hor_to() takes the last visible column before it
    zsvsheet_move_hor_to(di, zsvsheet_ui_buffer_visible_col(uib, col_offset + zsvsheet_struct_op_pos(op), max_cols));
}

// Insert or delete the row or column at the cursor (after: an insert goes below or right of it)
static void zsvsheet_struct_op_run(struct zsvsheet_sheet_context *state, enum zsvsheet_struct_op_kind kind,
                                   enum zsvsheet_struct_axis axis, char after) {
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *uib = *di->ui_buffers.current;
  struct zsvsheet_cell_pos pos;
  if (zsvsheet_cell_at_cursor(uib, &pos)) { // e.g. the blank start screen
    zsvsheet_ui_buffer_set_status(uib, "No cell under the cursor");
    return;
  }
  struct zsvsheet_struct_op op = {.kind = kind, .axis = axis, .after = after};
  const char *refusal = zsvsheet_struct_op_refusal(state, uib, &pos, &op);
  if (refusal) {
    zsvsheet_ui_buffer_set_status(uib, refusal);
    return;
  }
  op.at = axis == zsvsheet_struct_axis_row ? pos.raw_row : zsvsheet_cell_data_col(uib, &pos);

  char err[256];
  enum zsvsheet_rewrite_status rc = zsvsheet_ui_buffer_rewrite(uib, &op, err, sizeof(err));
  if (rc == zsvsheet_rewrite_failed) {
    zsvsheet_ui_buffer_set_statusf(uib, "Not done: %s", err);
    return;
  }
  const char *note = rc == zsvsheet_rewrite_header_unshown ? err : NULL;
  uib->modified = 1;
  uib->dimensions.col_count = op.widest; // the file's, now that every row has been read
  const size_t col_offset = zsvsheet_ui_buffer_data_col_offset(uib);
  if (axis == zsvsheet_struct_axis_col) {
    const char insert = kind == zsvsheet_struct_op_insert;
    const size_t col = zsvsheet_struct_op_pos(&op); // the inserted or deleted column
    zsvsheet_hidden_cols_shift(&uib->hidden_cols, col_offset + col, insert);
    if (insert && zsvsheet_ui_buffer_fit_cols(uib, col + 1) != zsvsheet_priv_status_ok && !note) // to reach it
      note = "Out of memory: the new column is not shown";
  }
  // columns a row delete took away are no longer hidden; if what is left is all hidden, show it
  zsvsheet_hidden_cols_truncate(&uib->hidden_cols, col_offset + uib->dimensions.col_count);
  if (!zsvsheet_visible_data_cols(uib)) {
    zsvsheet_hidden_cols_clear(&uib->hidden_cols);
    if (!note)
      note = "Every column left was hidden; all are shown";
  }
  zsvsheet_check_buffer_updates(uib, NULL, state); // the row count, from the new file's index
  di->update_buffer = 1;
  zsvsheet_struct_op_place_cursor(di, uib, &op);
  if (note)
    zsvsheet_ui_buffer_set_status(uib, note);
  else
    zsvsheet_modified_status(state, uib);
}

// :insert-row [above], :delete-row, :insert-column [left], :delete-column, and their <ctrl>o keys
static zsvsheet_status zsvsheet_struct_op_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  const char row = ctx->proc_id == zsvsheet_builtin_proc_insert_row || ctx->proc_id == zsvsheet_builtin_proc_delete_row;
  const char insert =
    ctx->proc_id == zsvsheet_builtin_proc_insert_row || ctx->proc_id == zsvsheet_builtin_proc_insert_col;
  const char *before = row ? "above" : "left";
  char after = 1;
  if (ctx->invocation.type == zsvsheet_proc_invocation_type_keypress)
    after = ctx->invocation.u.keypress.ch != ZSVSHEET_STRUCT_KEY_ROW_BEFORE &&
            ctx->invocation.u.keypress.ch != ZSVSHEET_STRUCT_KEY_COL_BEFORE;
  else if (ctx->num_params) {
    if (!insert || ctx->num_params > 1 || strcmp(ctx->params[0].u.string, before)) {
      zsvsheet_ui_buffer_set_statusf(*state->display_info.ui_buffers.current, "Usage: :%s-%s%s%s",
                                     insert ? "insert" : "delete", row ? "row" : "column", insert ? " " : "",
                                     insert ? (row ? "[above]" : "[left]") : "");
      return zsvsheet_status_ok;
    }
    after = 0;
  }
  zsvsheet_struct_op_run(state, insert ? zsvsheet_struct_op_insert : zsvsheet_struct_op_delete,
                         row ? zsvsheet_struct_axis_row : zsvsheet_struct_axis_col, after);
  return zsvsheet_status_ok;
}

// What the footer says while a <ctrl>o sequence waits for its next key, or NULL if none does.
// It, the cancel messages in zsvsheet_struct_key() and the help rows in key-bindings.c name the
// ZSVSHEET_STRUCT_KEY_* keys
static const char *zsvsheet_struct_prefix_hint(enum zsvsheet_struct_prefix prefix) {
  switch (prefix) {
  case zsvsheet_struct_prefix_ctrl_o:
    return "<ctrl>o: o/O row below/above, c/C column right/left, d delete";
  case zsvsheet_struct_prefix_ctrl_o_d:
    return "<ctrl>o d: d (or r) row, c column";
  default:
    return NULL;
  }
}

// A key of a <ctrl>o sequence, in either mode, before the mode sees it. Returns 1 if ch was
// one (with the outcome in *status), else 0. A sequence waits for its next key as long as it
// takes; <esc>, or <ctrl>o again, cancels it, and any other key cancels it and says so
static char zsvsheet_struct_key(int ch, struct zsvsheet_sheet_context *state, zsvsheet_status *status) {
  *status = zsvsheet_status_ok;
  const enum zsvsheet_struct_prefix prefix = state->struct_prefix;
  if (prefix == zsvsheet_struct_prefix_none) {
    if (ch != ZSVSHEET_KEY_STRUCT)
      return 0;
    state->struct_prefix = zsvsheet_struct_prefix_ctrl_o;
    zsvsheet_key_press_reset(); // the key before <ctrl>o does not pair with the key after the sequence
    return 1;
  }
  if (ch == KEY_RESIZE)
    return 0; // handled as usual; the sequence keeps waiting

  state->struct_prefix = zsvsheet_struct_prefix_none;
  zsvsheet_proc_id_t proc = ZSVSHEET_PROC_INVALID;
  if (prefix == zsvsheet_struct_prefix_ctrl_o) {
    switch (ch) {
    case ZSVSHEET_STRUCT_KEY_ROW:
    case ZSVSHEET_STRUCT_KEY_ROW_BEFORE:
      proc = zsvsheet_builtin_proc_insert_row;
      break;
    case ZSVSHEET_STRUCT_KEY_COL:
    case ZSVSHEET_STRUCT_KEY_COL_BEFORE:
      proc = zsvsheet_builtin_proc_insert_col;
      break;
    case ZSVSHEET_STRUCT_KEY_DELETE:
      state->struct_prefix = zsvsheet_struct_prefix_ctrl_o_d;
      return 1;
    }
  } else if (ch == ZSVSHEET_STRUCT_KEY_DELETE || ch == ZSVSHEET_STRUCT_KEY_DELETE_ROW)
    proc = zsvsheet_builtin_proc_delete_row;
  else if (ch == ZSVSHEET_STRUCT_KEY_COL)
    proc = zsvsheet_builtin_proc_delete_col;

  if (proc != ZSVSHEET_PROC_INVALID)
    *status = zsvsheet_proc_invoke_from_keypress(proc, ch, state);
  else if (ch != ZSVSHEET_KEY_ESC && ch != ZSVSHEET_KEY_STRUCT)
    zsvsheet_ui_buffer_set_status(*state->display_info.ui_buffers.current,
                                  prefix == zsvsheet_struct_prefix_ctrl_o
                                    ? "Cancelled: <ctrl>o takes o, O, c, C or d"
                                    : "Cancelled: <ctrl>o d takes d or r (row), or c (column)");
  return 1;
}
