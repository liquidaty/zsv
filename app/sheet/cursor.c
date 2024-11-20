static size_t input_offset_top(void) {
  return 0;
}

static size_t input_offset_bottom(struct zsvsheet_input_dimensions *input_dims,
                                  size_t buffer_row_count // number of rows in the buffer, w/o regard to header
) {
  return input_dims->row_count - buffer_row_count;
}

static size_t input_offset_centered(struct zsvsheet_input_dimensions *input_dims,
                                    size_t buffer_row_count, // number of rows in the buffer, w/o regard to header
                                    size_t target_raw_ix     // raw row index starting at zero, w/o regard to header
) {
  if (input_dims->row_count <= buffer_row_count) // entire input fits in buffer
    return input_offset_top();
  size_t buffer_half_row_count = buffer_row_count / 2;
  if (target_raw_ix <= buffer_half_row_count) // target is at the top of the file
    return input_offset_top();
  if (target_raw_ix + buffer_half_row_count > input_dims->row_count) // target is at the end of the file
    return input_offset_bottom(input_dims, buffer_row_count);
  return target_raw_ix - buffer_half_row_count;
}

// zsvsheet_goto_input_raw_row
// return non-zero if buffer must be reloaded
/*
      ------- INPUT
      0  | header   | => BUFFER[BHEADSPAN]
      1  | ...      | => BUFFER[BHEADSPAN]
      2  | data
      ------- BUFFER[BHEADSPAN:]
ioff->3  |
      4  |
      ------- DISPLAY DATA
      5  | boff-> | displayed data 1
      6  |        | displayed data 2
      ...
      N+4|        | displayed data M = max_y - BHEADSPAN - 1
      -------
 */

static void set_window_to_cursor(struct zsvsheet_rowcol *buff_offset, size_t target_raw_row,
                                 struct zsvsheet_rowcol *input_offset, size_t input_header_span,
                                 struct zsvsheet_display_dimensions *ddims, size_t cursor_row) {
  // assume that input_offset->row is fixed; set buff_offset->row
  if (target_raw_row + ddims->header_span >= input_offset->row + cursor_row + input_header_span)
    buff_offset->row = target_raw_row - input_offset->row - cursor_row + ddims->header_span - input_header_span;
  else
    buff_offset->row = 0;
}

static int zsvsheet_goto_input_raw_row(struct zsvsheet_ui_buffer *uib, size_t input_raw_num, size_t input_header_span,
                                       struct zsvsheet_display_dimensions *ddims, size_t final_cursor_position) {
  zsvsheet_screen_buffer_t buffer = uib->buffer;
  struct zsvsheet_rowcol *input_offset = &uib->input_offset;
  struct zsvsheet_rowcol *buff_offset = &uib->buff_offset;
  struct zsvsheet_input_dimensions *input_dims = &uib->dimensions;
  size_t *cursor_rowp = &uib->cursor_row;

  size_t buffer_rows = zsvsheet_screen_buffer_rows(buffer);
  int update_buffer = 0;
  if (input_raw_num < input_offset->row + input_header_span                      // move the buffer up
      || input_raw_num + input_header_span + 1 > input_offset->row + buffer_rows // move the buffer down
  ) {
    input_offset->row = input_offset_centered(input_dims, buffer_rows, input_raw_num);
    update_buffer = 1;
  } else if (!(input_raw_num >= input_offset->row + buff_offset->row &&
               input_raw_num <=
                 input_offset->row + buff_offset->row + (ddims->rows + ddims->header_span + ddims->footer_span))) {
    // buffer already has the data but display window needs to move
  } else {
    if (final_cursor_position == (size_t)-1) {
      // just move the cursor however much we needed to shift
      size_t current = zsvsheet_get_input_raw_row(input_offset, buff_offset, *cursor_rowp);
      final_cursor_position = *cursor_rowp + input_raw_num - current;
    }
  }
  if (final_cursor_position == (size_t)-1)
    final_cursor_position = ddims->header_span;
  if (final_cursor_position < ddims->header_span)
    final_cursor_position = ddims->header_span;
  if (final_cursor_position > ddims->rows - ddims->footer_span - 1)
    final_cursor_position = ddims->rows - ddims->footer_span - 1;
  *cursor_rowp = final_cursor_position;

  // we may still need to update the buffer if the row we are jumping to will be
  // in the middle of the screen and therefore we still want to display additional
  // rows, and those additional rows are not loaded into the buffer
  if (!update_buffer && final_cursor_position < ddims->rows - 1) {
    size_t last_raw_row_to_display = input_raw_num + (ddims->rows - 1 - final_cursor_position);
    if (last_raw_row_to_display > input_dims->row_count)
      last_raw_row_to_display = input_dims->row_count;
    if (last_raw_row_to_display < input_offset->row + input_header_span                      // move the buffer up
        || last_raw_row_to_display + input_header_span + 1 > input_offset->row + buffer_rows // move the buffer down
    ) {
      input_offset->row = input_offset_centered(input_dims, buffer_rows, input_raw_num);
      update_buffer = 1;
    }
  }
  set_window_to_cursor(buff_offset, input_raw_num, input_offset, input_header_span, ddims, *cursor_rowp);
  return update_buffer;
}

static int cursor_right(int max_x, unsigned display_width, size_t max_col_count, size_t *cursor_colp,
                        size_t *start_colp) {
  size_t cursor_col = *cursor_colp;
  size_t start_col = *start_colp;
  int rc = 1;
  if (cursor_col + 1 < max_x / display_width && start_col + cursor_col + 1 < max_col_count) {
    cursor_col++;
  } else if ((start_col + max_x / display_width) < max_col_count) {
    start_col++;
  } else
    rc = 0;
  *cursor_colp = cursor_col;
  *start_colp = start_col;
  return rc;
}
