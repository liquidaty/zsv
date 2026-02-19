#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>

#include <zsv.h>

#include "sheet/curses.h"

#include <locale.h>
#include <wchar.h>
#include <pthread.h>

#define ZSV_COMMAND sheet
#include "zsv_command.h"

#include "../include/zsv/ext/sheet.h"
#include "../include/zsv/utils/string.h"
#include "sheet/sheet_internal.h"
#include "sheet/screen_buffer.c"
#include "sheet/lexer.c"
#include "sheet/procedure.c"

/* TODO: move this somewhere else like common or utils */
#define UNUSED(X) ((void)X)

struct zsvsheet_opts {
  char hide_row_nums; // if 1, will load row nums
  const char *find;
  size_t find_specified_column_plus_1; // if 0, find in any column; else only search specified column (1-based)
  char find_exact; // later: make this a bitfield with different options e.g. case-insensitive, regex etc
  size_t found_rownum;
  size_t found_colnum;
};

#include "sheet/utf8-width.c"
#include "sheet/ui_buffer.c"
#include "sheet/index.c"
#include "sheet/read-data.c"
#include "sheet/key-bindings.c"

#define ZSVSHEET_CELL_DISPLAY_MIN_WIDTH 10
static size_t zsvsheet_cell_display_width(struct zsvsheet_ui_buffer *ui_buffer,
                                          struct zsvsheet_display_dimensions *ddims) {
  size_t width = ddims->columns /
                 (ui_buffer->dimensions.col_count + (ui_buffer->rownum_col_offset && !ui_buffer->has_row_num ? 1 : 0));
  return width < ZSVSHEET_CELL_DISPLAY_MIN_WIDTH ? ZSVSHEET_CELL_DISPLAY_MIN_WIDTH : width;
}

static void display_buffer_subtable(struct zsvsheet_ui_buffer *ui_buffer, size_t input_header_span,
                                    struct zsvsheet_display_dimensions *ddims);

static void zsvsheet_priv_set_status(const struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt,
                                     ...);

struct zsvsheet_display_info {
  int update_buffer;
  struct zsvsheet_display_dimensions *dimensions;
  size_t header_span;
  struct {
    struct zsvsheet_ui_buffer **base;
    struct zsvsheet_ui_buffer **current;
  } ui_buffers;
};

struct zsvsheet_sheet_context {
  struct zsvsheet_display_info display_info;
  char *find;
  char *goto_column;
  struct zsv_prop_handler *custom_prop_handler;
};

static void get_subcommand(const char *prompt, char *buff, size_t buffsize, int footer_row, const char *default_value) {
  *buff = '\0';

  // this is a hack to blank-out the currently-selected cell value
  int max_screen_width = 256; // to do: don't assume this
  for (int i = 0; i < max_screen_width; i++)
    mvprintw(footer_row, i, "%c", ' ');

  if (default_value && *default_value)
    mvprintw(footer_row, 0, "%s: %s", prompt, default_value);
  else
    mvprintw(footer_row, 0, "%s: ", prompt);

  int ch;
  int y, x;
  getyx(stdscr, y, x); // Get the current cursor position after the prompt
  if (default_value) {
    strncpy(buff, default_value, buffsize);
    buff[buffsize - 1] = '\0';
  }
  size_t idx = default_value ? strlen(default_value) : 0;
  if (default_value)
    x -= strlen(default_value);
  while (1) {
    ch = getch();                          // Read a character from the user
    if (ch == 27) {                        // escape
      buff[0] = '\0';                      // Clear buffer & exit
      break;                               // Exit the loop
    } else if (ch == '\n' || ch == '\r') { // ENTER key
      buff[idx] = '\0';                    // Null-terminate the string
      break;                               // Exit the loop
    } else if (ch == ZSVSHEET_CTRL('A')) {
      while (idx > 0) {
        idx--;
        buff[idx] = '\0';
        mvwdelch(stdscr, y, x + idx); // Move cursor back to start
      }
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') { // BACKSPACE key
      if (idx > 0) {
        idx--;
        buff[idx] = '\0';
        mvwdelch(stdscr, y, x + idx); // Move cursor back and delete character
      }
    } else if (isprint(ch)) { // Printable character
      if (idx < buffsize - 1) {
        buff[idx++] = ch;
        buff[idx] = '\0';
        addch(ch); // Echo the character
      }
    }
    // Ignore other keys
  }
}

zsvsheet_status zsvsheet_ext_prompt(struct zsvsheet_proc_context *ctx, char *buffer, size_t bufsz, const char *fmt,
                                    ...) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;

  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  char prompt_buffer[256] = {0};

  va_list argv;
  va_start(argv, fmt);
  int n = vsnprintf(prompt_buffer, sizeof(prompt_buffer), fmt, argv);
  va_end(argv);

  if (!(n > 0 && (size_t)n < sizeof(prompt_buffer)))
    return zsvsheet_status_ok;

  get_subcommand(prompt_buffer, buffer, bufsz, prompt_footer_row, NULL);
  if (*prompt_buffer == '\0')
    return zsvsheet_status_ok;

  return zsvsheet_status_ok;
}

size_t zsvsheet_get_input_raw_row(struct zsvsheet_rowcol *input_offset, struct zsvsheet_rowcol *buff_offset,
                                  size_t cursor_row) {
  return input_offset->row + buff_offset->row + cursor_row;
}

#include "sheet/cursor.c"

struct zsvsheet_display_dimensions get_display_dimensions(size_t header_span, size_t footer_span) {
  struct zsvsheet_display_dimensions dims = {0};
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  if (max_y < (int)(header_span + footer_span + 1))
    dims.rows = header_span + footer_span + 1;
  else
    dims.rows = (size_t)max_y;
  if (max_x < 12)
    dims.columns = 12;
  else
    dims.columns = (size_t)max_x;
  dims.header_span = header_span;
  dims.footer_span = footer_span;
  return dims;
}

size_t display_data_rowcount(struct zsvsheet_display_dimensions *dims) {
  return dims->rows - dims->footer_span - dims->header_span;
}

char zsvsheet_status_text[256] = {0};
static void zsvsheet_display_status_text(const struct zsvsheet_display_dimensions *ddims) {
  // clear the entire line
  mvprintw(ddims->rows - ddims->footer_span, 0, "%-*s", (int)sizeof(zsvsheet_status_text), "");

  // add status, highlighted
  attron(A_REVERSE);
  mvprintw(ddims->rows - ddims->footer_span, 0, "%s", zsvsheet_status_text);
  attroff(A_REVERSE);
}

static void zsvsheet_priv_set_status(const struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt,
                                     ...) {
  if (overwrite || !*zsvsheet_status_text) {
    va_list argv;
    va_start(argv, fmt);
    int n = vsnprintf(zsvsheet_status_text, sizeof(zsvsheet_status_text), fmt, argv);
    if (n > 0 && (size_t)(n + 2) < sizeof(zsvsheet_status_text) && zsvsheet_status_text[n - 1] != ' ') {
      zsvsheet_status_text[n] = ' ';
      zsvsheet_status_text[n + 1] = '\0';
    }
    va_end(argv);
    // note: if (n < (int)sizeof(zsvsheet_status_text)), then we just ignore
  }
  zsvsheet_display_status_text(ddims);
}

#include "sheet/terminfo.c"
#include "sheet/handlers_internal.h"
#include "sheet/handlers.c"
#include "sheet/file.c"
#include "sheet/usage.c"
#include "sheet/transformation.c"

struct zsvsheet_key_data *zsvsheet_key_handlers = NULL;
struct zsvsheet_key_data **zsvsheet_next_key_handler = &zsvsheet_key_handlers;

/* Common page movement function */
// TO DO: get rid of di->header_span. Just always assume it is 1
static zsvsheet_status zsvsheet_move_page(struct zsvsheet_display_info *di, bool up) {
  size_t current, target;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);

  if (up && current_ui_buffer->dimensions.row_count <= di->header_span)
    return zsvsheet_status_ok; // no data

  current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                       current_ui_buffer->cursor_row);

  if (up) {
    if (current <= display_data_rowcount(di->dimensions) + di->header_span) {
      return zsvsheet_status_ok; // already at top
    } else {
      target = current - display_data_rowcount(di->dimensions);
      if (target >= current_ui_buffer->dimensions.row_count)
        target = current_ui_buffer->dimensions.row_count > 0 ? current_ui_buffer->dimensions.row_count - 1 : 0;
    }
  } else {
    if (current >= current_ui_buffer->dimensions.row_count - display_data_rowcount(di->dimensions)) {
      return zsvsheet_status_ok; // already at bottom
    } else {
      target = current + display_data_rowcount(di->dimensions);
    }
  }

  di->update_buffer = zsvsheet_goto_input_raw_row(current_ui_buffer, target, di->header_span, di->dimensions,
                                                  current_ui_buffer->cursor_row);
  return zsvsheet_status_ok;
}

/* Common vertical movement function */
static zsvsheet_status zsvsheet_move_ver(struct zsvsheet_display_info *di, bool up) {
  size_t current;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);
  current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                       current_ui_buffer->cursor_row);
  if (up) {
    if (current > di->header_span) {
      di->update_buffer =
        zsvsheet_goto_input_raw_row(current_ui_buffer, current - 1, di->header_span, di->dimensions,
                                    current_ui_buffer->cursor_row > 0 ? current_ui_buffer->cursor_row - 1 : 0);
    } else if (current_ui_buffer->cursor_row > 0) {
      current_ui_buffer->cursor_row--;
    }
  } else {
    if (current >= current_ui_buffer->dimensions.row_count - 1)
      return zsvsheet_status_ok;
    di->update_buffer = zsvsheet_goto_input_raw_row(current_ui_buffer, current + 1, di->header_span, di->dimensions,
                                                    current_ui_buffer->cursor_row + 1);
  }
  return zsvsheet_status_ok;
}

/* Common horizontal movement function */
static zsvsheet_status zsvsheet_move_hor(struct zsvsheet_display_info *di, bool right) {
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);

  if (right) {
    cursor_right(di->dimensions->columns, zsvsheet_cell_display_width(current_ui_buffer, di->dimensions),
                 current_ui_buffer->dimensions.col_count + current_ui_buffer->rownum_col_offset >
                     zsvsheet_screen_buffer_cols(current_ui_buffer->buffer)
                   ? zsvsheet_screen_buffer_cols(current_ui_buffer->buffer)
                   : current_ui_buffer->dimensions.col_count + current_ui_buffer->rownum_col_offset,
                 &current_ui_buffer->cursor_col, &current_ui_buffer->buff_offset.col);
  } else {
    if (current_ui_buffer->cursor_col > 0) {
      current_ui_buffer->cursor_col--;
    } else if (current_ui_buffer->buff_offset.col > 0) {
      current_ui_buffer->buff_offset.col--;
    }
  }
  return zsvsheet_status_ok;
}

/* Common vertical movement between extremes */
static zsvsheet_status zsvsheet_move_ver_end(struct zsvsheet_display_info *di, bool top) {
  size_t current;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);

  current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                       current_ui_buffer->cursor_row);
  UNUSED(current);

  if (top) {
    di->update_buffer =
      zsvsheet_goto_input_raw_row(current_ui_buffer, 1, di->header_span, di->dimensions, di->dimensions->header_span);
  } else {
    if (current_ui_buffer->dimensions.row_count == 0)
      return zsvsheet_status_ok;
    if (current_ui_buffer->dimensions.row_count <= di->dimensions->rows - di->dimensions->footer_span) {
      current_ui_buffer->cursor_row = current_ui_buffer->dimensions.row_count - 1;
    } else {
      di->update_buffer =
        zsvsheet_goto_input_raw_row(current_ui_buffer, current_ui_buffer->dimensions.row_count - 1, di->header_span,
                                    di->dimensions, di->dimensions->rows - di->dimensions->header_span - 1);
    }
  }
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_move_hor_to(struct zsvsheet_display_info *di, size_t move_to) {
  // to do: directly set current_ui_buffer->cursor_col and buff_offset.col
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);
  enum zsvsheet_status stat = zsvsheet_status_ok;
  size_t start_col = current_ui_buffer->buff_offset.col;
  size_t current_absolute_col = start_col + current_ui_buffer->cursor_col;
  if (current_absolute_col < move_to) {
    for (size_t i = 0, j = move_to - current_absolute_col; i < j; i++) {
      if ((stat = zsvsheet_move_hor(di, true)) != zsvsheet_status_ok)
        break;
    }
  } else if (current_absolute_col > move_to) {
    for (size_t i = 0, j = current_absolute_col - move_to; i < j; i++) {
      if ((stat = zsvsheet_move_hor(di, false)) != zsvsheet_status_ok)
        break;
    }
  }
  return stat;
}

/* Common horizontal movement between extremes */
static zsvsheet_status zsvsheet_move_hor_end(struct zsvsheet_display_info *di, bool right) {
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);
  if (right)
    zsvsheet_move_hor_to(di, current_ui_buffer->dimensions.col_count);
  else
    zsvsheet_move_hor_to(di, 0);
  return zsvsheet_status_ok;
}

// zsvsheet_handle_find_next: return non-zero if a result was found
static char zsvsheet_handle_find_next(struct zsvsheet_display_info *di, struct zsvsheet_ui_buffer *uib,
                                      const char *needle, size_t specified_column_plus_1, char find_exact,
                                      size_t header_span, struct zsvsheet_display_dimensions *ddims, int *update_buffer,
                                      struct zsv_prop_handler *custom_prop_handler) {
  struct zsvsheet_opts zsvsheet_opts = {0};
  zsvsheet_opts.find = needle;
  zsvsheet_opts.find_specified_column_plus_1 = specified_column_plus_1;
  zsvsheet_opts.find_exact = find_exact;
  zsvsheet_opts.found_rownum = 0;
  zsvsheet_opts.found_colnum = uib->cursor_col + uib->buff_offset.col;
  if (zsvsheet_find_next(uib, &zsvsheet_opts, header_span, custom_prop_handler) > 0) {
    *update_buffer = zsvsheet_goto_input_raw_row(uib, zsvsheet_opts.found_rownum, header_span, ddims, (size_t)-1);

    // move to zsvsheet_opts->found_colnum; + 1 to skip the "Row #" column
    zsvsheet_move_hor_to(di, zsvsheet_opts.found_colnum + 1);
    return 1;
  }
  zsvsheet_priv_set_status(ddims, 1, "Not found");
  return 0;
}

/* Find column handler: case-insensitive column header find */
static zsvsheet_status zsvsheet_goto_column(struct zsvsheet_sheet_context *state) {
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);
  if (zsvsheet_buffer_data_filename(current_ui_buffer)) {
    char prompt_buffer[256] = {0};
    int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
    get_subcommand("Find column", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, state->goto_column);
    if (*prompt_buffer != '\0') {
      free(state->goto_column);
      state->goto_column = strdup(prompt_buffer);
      if (state->goto_column) {
        size_t len_lc = strlen(state->goto_column);
        int err;
        unsigned char *find_lc = zsv_strtolowercase_w_err((const unsigned char *)state->goto_column, &len_lc, &err);
        if (find_lc && len_lc) {
          zsvsheet_screen_buffer_t buffer = current_ui_buffer->buffer;
          size_t colcount = zsvsheet_screen_buffer_cols(buffer);
          size_t found = 0; // if found, will equal 1 + column index
          for (size_t i = 0; !found && i < colcount; i++) {
            const unsigned char *colname = zsvsheet_screen_buffer_cell_display(buffer, 0, i);
            if (colname && *colname) {
              size_t colname_len_lc = strlen((const char *)colname);
              unsigned char *colname_lc = zsv_strtolowercase_w_err(colname, &colname_len_lc, &err);
              if (colname_lc && colname_len_lc > 0 && memmem(colname_lc, colname_len_lc, find_lc, len_lc))
                found = i + 1;
              free(colname_lc);
            }
          }
          free(find_lc);
          if (found)
            zsvsheet_move_hor_to(di, found - 1);
        }
      }
    }
  }
  return 0;
}

/* Find and find-next handler */
static zsvsheet_status zsvsheet_find(struct zsvsheet_sheet_context *state, bool next) {
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);

  if (!zsvsheet_buffer_data_filename(current_ui_buffer))
    goto out;

  if (!next) {
    char prompt_buffer[256] = {0};
    int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
    // to do: support regex
    get_subcommand("Find", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL);
    if (*prompt_buffer == '\0') {
      goto out;
    } else {
      free(state->find);
      state->find = strdup(prompt_buffer);
    }
  }

  if (state->find) {
    zsvsheet_handle_find_next(di, current_ui_buffer, state->find, 0, 0, // any column, non-exact
                              di->header_span, di->dimensions, &di->update_buffer, state->custom_prop_handler);
  }

out:
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_open_file_handler(struct zsvsheet_proc_context *ctx) {
  // TODO: should be PATH_MAX but that's going to be about a page and compiler
  //       might complain about stack being too large. Probably move to handler
  //       state or something.
  // TODO: allow additional zsv options
  char prompt_buffer[256] = {0};
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  const char *filename;

  struct zsvsheet_display_info *di = &state->display_info;
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  int err;

  UNUSED(ctx);

  if (ctx->num_params > 0) {
    filename = strdup(ctx->params[0].u.string);
  } else {
    if (!ctx->invocation.interactive)
      return zsvsheet_status_error;
    get_subcommand("File to open", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL);
    if (*prompt_buffer == '\0')
      goto no_input;
    filename = strdup(prompt_buffer);
  }

  if ((err = zsvsheet_ui_buffer_open_file(filename, NULL, state->custom_prop_handler, di->ui_buffers.base,
                                          di->ui_buffers.current))) {
    if (err > 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "%s: %s", filename, strerror(err));
    else if (err < 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "Unexpected error");
    else
      zsvsheet_priv_set_status(di->dimensions, 1, "Not found: %s", filename);
    return zsvsheet_status_ignore;
  }
no_input:
  return zsvsheet_status_ok;
}

#include "sheet/filter.c"

static zsvsheet_status zsvsheet_filter_handler(struct zsvsheet_proc_context *ctx) {
  char prompt_buffer[256] = {0};
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *current_ui_buffer = *state->display_info.ui_buffers.current;
  size_t single_column = 0;
  if (ctx->proc_id == zsvsheet_builtin_proc_filter_this) {
    struct zsvsheet_rowcol rc;
    zsvsheet_buffer_t buff = zsvsheet_buffer_current(ctx);
    if (zsvsheet_buffer_get_selected_cell(buff, &rc) != zsvsheet_status_ok)
      return zsvsheet_status_error;
    single_column = rc.col;
  }
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  struct zsvsheet_buffer_info_internal binfo = zsvsheet_buffer_info_internal(current_ui_buffer);
  const char *filter;

  if (binfo.write_in_progress && !binfo.write_done)
    return zsvsheet_status_busy;

  if (!zsvsheet_buffer_data_filename(current_ui_buffer))
    goto out;

  if (ctx->num_params > 0) {
    filter = ctx->params[0].u.string;
  } else {
    if (!ctx->invocation.interactive)
      return zsvsheet_status_error;
    if (single_column)
      get_subcommand("Filter (this column)", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL);
    else
      get_subcommand("Filter", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL);
    if (*prompt_buffer == '\0')
      goto out;
    filter = prompt_buffer;
  }

  return zsvsheet_filter_file(ctx, filter, single_column);
out:
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_subcommand_handler(struct zsvsheet_proc_context *ctx) {
  char prompt_buffer[256] = {0};
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);

  get_subcommand("", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row, NULL);
  if (*prompt_buffer == '\0')
    return zsvsheet_status_ok;

  struct zsvsheet_proc_context context = {
    .invocation.interactive = true,
    .invocation.type = zsvsheet_proc_invocation_type_proc,
    .invocation.u.proc.id = ctx->proc_id,
    .subcommand_context = ctx->subcommand_context,
  };
  zsvsheet_status rc = zsvsheet_proc_invoke_from_command(prompt_buffer, &context);
  return rc;
}

static zsvsheet_status zsvsheet_buffer_new_static(struct zsvsheet_proc_context *ctx, const size_t cols,
                                                  const unsigned char **(*get_header)(void *cbctx),
                                                  const unsigned char **(*get_row)(void *cbctx),
                                                  const unsigned char *(*get_status)(void *cbctx), void *cbctx,
                                                  struct zsvsheet_screen_buffer_opts *boptsp) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_screen_buffer_opts bopts = {
    .no_rownum_column = 1,
    .cell_buff_len = 64,
    .max_cell_len = 0,
    .rows = 256,
  };
  if (boptsp)
    bopts = *boptsp;
  struct zsvsheet_ui_buffer_opts uibopts = {
    .buff_opts = &bopts,
    .filename = NULL,
    .data_filename = NULL,
    .no_rownum_col_offset = 1,
    .write_after_open = 0,
  };
  struct zsvsheet_ui_buffer *uib = NULL;
  zsvsheet_screen_buffer_t buffer;
  enum zsvsheet_priv_status pstat;
  enum zsvsheet_status stat = zsvsheet_status_error;

  buffer = zsvsheet_screen_buffer_new(cols, &bopts, &pstat);
  if (pstat != zsvsheet_priv_status_ok)
    goto free_buffer;

  uib = zsvsheet_ui_buffer_new(buffer, &uibopts);
  if (!uib)
    goto free_buffer;

  const unsigned char **head = get_header(cbctx);
  for (size_t j = 0; j < cols && head[j]; j++) {
    if (*head[j])
      pstat = zsvsheet_screen_buffer_write_cell(buffer, 0, j, head[j]);
    if (pstat != zsvsheet_priv_status_ok)
      goto free_buffer;
  }

  size_t row = 1;
  const unsigned char **row_data;
  while ((row_data = get_row(cbctx)) != NULL) {
    for (size_t j = 0; j < cols && row_data[j]; j++) {
      if (*row_data[j]) {
        pstat = zsvsheet_screen_buffer_write_cell(buffer, row, j, row_data[j]);
        if (pstat != zsvsheet_priv_status_ok)
          goto free_buffer;
      }
    }
    row++;
  }

  uib->dimensions.col_count = cols;
  uib->dimensions.row_count = row;
  uib->buff_used_rows = row;

  const unsigned char *status = get_status(cbctx);
  if (status)
    zsvsheet_ui_buffer_set_status(uib, (const char *)status);

  zsvsheet_ui_buffer_push(di->ui_buffers.base, di->ui_buffers.current, uib);
  stat = zsvsheet_status_ok;
  goto out;

free_buffer:
  if (uib)
    zsvsheet_ui_buffer_delete(uib);
  else
    zsvsheet_screen_buffer_delete(buffer);
out:
  return stat;
}

#include "sheet/help.c"
#include "sheet/errors.c"

#include "sheet/pivot.c"
#include "sheet/sqlfilter.c"
#include "sheet/newline_handler.c"

/* We do most procedures in one handler. More complex procedures can be
 * separated into their own handlers.
 */
zsvsheet_status zsvsheet_builtin_proc_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_sheet_context *state = (struct zsvsheet_sheet_context *)ctx->subcommand_context;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(state->display_info.ui_buffers.current);

  switch (ctx->proc_id) {
  case zsvsheet_builtin_proc_quit:
    return zsvsheet_status_exit;
  case zsvsheet_builtin_proc_resize:
    *(state->display_info.dimensions) = get_display_dimensions(1, 1);
    break;

  case zsvsheet_builtin_proc_move_up:
    return zsvsheet_move_ver(&state->display_info, true);
  case zsvsheet_builtin_proc_move_down:
    return zsvsheet_move_ver(&state->display_info, false);
  case zsvsheet_builtin_proc_move_right:
    return zsvsheet_move_hor(&state->display_info, true);
  case zsvsheet_builtin_proc_move_left:
    return zsvsheet_move_hor(&state->display_info, false);

  case zsvsheet_builtin_proc_pg_up:
    return zsvsheet_move_page(&state->display_info, true);
  case zsvsheet_builtin_proc_pg_down:
    return zsvsheet_move_page(&state->display_info, false);

  case zsvsheet_builtin_proc_move_top:
    return zsvsheet_move_ver_end(&state->display_info, true);
  case zsvsheet_builtin_proc_move_bottom:
    return zsvsheet_move_ver_end(&state->display_info, false);

  case zsvsheet_key_move_last_col:
    return zsvsheet_move_hor_end(&state->display_info, true);
  case zsvsheet_key_move_first_col:
    return zsvsheet_move_hor_end(&state->display_info, false);

  case zsvsheet_builtin_proc_escape:
    // current_ui_buffer is not the base/blank buffer
    if (current_ui_buffer->prior) {
      if (zsvsheet_ui_buffer_pop(state->display_info.ui_buffers.base, state->display_info.ui_buffers.current, NULL)) {
        state->display_info.update_buffer = 1;
      }
    }
    break;

  case zsvsheet_builtin_proc_find:
    return zsvsheet_find(state, false);
  case zsvsheet_builtin_proc_find_next:
    return zsvsheet_find(state, true);
  case zsvsheet_builtin_proc_goto_column:
    return zsvsheet_goto_column(state);
  }

  return zsvsheet_status_error;
}

/* clang-format off */
struct builtin_proc_desc {
  int proc_id;
  const char *name;
  const char *description;
  zsvsheet_proc_fn handler;
} builtin_procedures[] = {
  { zsvsheet_builtin_proc_quit,           "quit",        "Exit the application",                                            zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_escape,         "escape",      "Leave the current view or cancel a subcommand",                   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_bottom,    "bottom",      "Jump to the last row",                                            zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_top,       "top",         "Jump to the first row",                                           zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_first_col, "first",       "Jump to the first column",                                        zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_pg_down,        "pagedown",    "Move down one page",                                              zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_pg_up,          "pageup",      "Move up one page",                                                zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_last_col,  "last",        "Jump to the last column",                                         zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_up,        "up",          "Move up one row",                                                 zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_down,      "down",        "Move down one row",                                               zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_left,      "left",        "Move left one column",                                            zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_right,     "right",       "Move right one column",                                           zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_find,           "find",        "Set a search term and jump to the first result after the cursor", zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_find_next,      "next",        "Jump to the next search result",                                  zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_goto_column,    "gotocolumn",  "Go to column",                                                    zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_resize,         "resize",      "Resize the layout to fit new terminal dimensions",                zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_open_file,      "open",        "Open another CSV file",                                           zsvsheet_open_file_handler    },
  { zsvsheet_builtin_proc_filter,         "filter",      "Filter by specified text",                                        zsvsheet_filter_handler       },
  { zsvsheet_builtin_proc_filter_this,    "filtercol",   "Filter by specified text only in current column",                 zsvsheet_filter_handler       },
  { zsvsheet_builtin_proc_sqlfilter,      "where",       "Filter by sql expression",                                        zsvsheet_sqlfilter_handler    },
  { zsvsheet_builtin_proc_subcommand,     "subcommand",  "Editor subcommand",                                               zsvsheet_subcommand_handler   },
  { zsvsheet_builtin_proc_help,           "help",        "Display a list of actions and key-bindings",                      zsvsheet_help_handler         },
  { zsvsheet_builtin_proc_newline,        "<Enter>",     "Follow hyperlink (if any)",                                       zsvsheet_newline_handler      },
  { zsvsheet_builtin_proc_pivot_cur_col,  "pivot",       "Group rows by the column under the cursor",                       zsvsheet_pivot_handler        },
  { zsvsheet_builtin_proc_pivot_expr,     "pivotexpr",   "Group rows with group-by SQL expression",                         zsvsheet_pivot_handler        },
  { zsvsheet_builtin_proc_errors,         "errors",      "Show errors (if any)",                                            zsvsheet_errors_handler       },
  { zsvsheet_builtin_proc_errors_clear,   "errors-clear","Clear any/all errors",                                            zsvsheet_errors_handler       },
  { -1, NULL, NULL, NULL }
};
/* clang-format on */

void zsvsheet_register_builtin_procedures(void) {
  for (struct builtin_proc_desc *desc = builtin_procedures; desc->proc_id != -1; ++desc) {
    if (zsvsheet_register_builtin_proc(desc->proc_id, desc->name, desc->description, desc->handler) < 0) {
      fprintf(stderr, "Failed to register builtin procedure\n");
    }
  }
}

static void zsvsheet_check_buffer_worker_updates(struct zsvsheet_ui_buffer *ub,
                                                 struct zsvsheet_display_dimensions *display_dims,
                                                 struct zsvsheet_sheet_context *handler_state) {
  pthread_mutex_lock(&ub->mutex);
  if (ub->status) {
    if (display_dims)
      zsvsheet_priv_set_status(display_dims, 1, "%s", ub->status);
  }
  if (ub->index_ready && ub->dimensions.row_count != ub->index->row_count + 1) {
    ub->dimensions.row_count = ub->index->row_count + 1;
    if (handler_state)
      handler_state->display_info.update_buffer = true;
  }
  pthread_mutex_unlock(&ub->mutex);
}

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsvsheet_usage();
    return zsv_status_ok;
  }

#if defined(WIN32) || defined(_WIN32)
  if (setenv("TERM", "", 1) != 0)
    fprintf(stderr, "Warning: unable to unset TERM env var\n");
#endif

  const char *locale = setlocale(LC_ALL, "C.UTF-8");
  if (!locale || strstr(locale, "UTF-8") == NULL)
    locale = setlocale(LC_ALL, "");
#if !defined(WIN32) && !defined(_WIN32)
  if (!locale || strstr(locale, "UTF-8") == NULL) {
    fprintf(stderr, "Unable to set locale to UTF-8\n");
    return 1; // TO DO: option to continue
  }
#endif

  if (!terminfo_ok()) {
    fprintf(stderr, "Warning: unable to set or detect TERMINFO\n");
    return 1;
  }

  struct zsvsheet_ui_buffer *ui_buffers = NULL;
  struct zsvsheet_ui_buffer *current_ui_buffer = NULL;

  size_t header_span = 0; // number of rows that comprise the header
  int err;
  {
    struct zsvsheet_ui_buffer *tmp_ui_buffer = NULL;
    zsvsheet_ui_buffer_new_blank(&tmp_ui_buffer);
    if (!tmp_ui_buffer) {
      fprintf(stderr, "Out of memory!\n");
      return ENOMEM;
    }
    zsvsheet_ui_buffer_push(&ui_buffers, &current_ui_buffer, tmp_ui_buffer);
  }

  if (argc > 1) {
    const char *filename = argv[1];
    if ((err = zsvsheet_ui_buffer_open_file(filename, optsp, custom_prop_handler, &ui_buffers, &current_ui_buffer))) {
      if (err > 0)
        perror(filename);
      else
        fprintf(stderr, "%s: no data found", filename); // to do: change this to a base-buff status msg

      err = -1;
      goto zsvsheet_exit;
    }
  }

  err = 0;
  header_span = 1;
  initscr();
  noecho();
  keypad(stdscr, TRUE);
  cbreak();
  set_escdelay(30);
  struct zsvsheet_display_dimensions display_dims = get_display_dimensions(1, 1);

  zsvsheet_register_builtin_procedures();

  /* TODO: allow user to pass key binding choice and dmux here */
  zsvsheet_register_vim_key_bindings();
  // zsvsheet_register_emacs
  //...
  //

  int ch;
  struct zsvsheet_sheet_context handler_state = {
    .display_info.ui_buffers.base = &ui_buffers,
    .display_info.ui_buffers.current = &current_ui_buffer,
    .display_info.dimensions = &display_dims,
    .display_info.header_span = header_span,
    .find = NULL,
    .custom_prop_handler = custom_prop_handler,
  };

  zsvsheet_status status;

#if defined(WIN32) || defined(_WIN32)
  // induce delay for index to complete before checking updates (observed under WSL)
  // maybe, need a thread coordination strategy using condition variable to proceed?
  napms(100);
#endif

  zsvsheet_check_buffer_worker_updates(current_ui_buffer, &display_dims, &handler_state);
  display_buffer_subtable(current_ui_buffer, header_span, &display_dims);

  // now ncurses getch() will fire every 2-tenths of a second so we can check for status update
  halfdelay(2);

  while (true) {
    ch = getch();

    handler_state.display_info.update_buffer = false;
    zsvsheet_priv_set_status(&display_dims, 1, "");

    if (ch != ERR) {
      status = zsvsheet_key_press(ch, &handler_state);
      if (status == zsvsheet_status_exit)
        break;
      if (status != zsvsheet_status_ok)
        continue;
    }

    struct zsvsheet_ui_buffer *ub = current_ui_buffer;
    zsvsheet_check_buffer_worker_updates(ub, &display_dims, &handler_state);

    if (handler_state.display_info.update_buffer && zsvsheet_buffer_data_filename(ub)) {
      struct zsvsheet_opts zsvsheet_opts = {0};
      if (read_data(&ub, NULL, current_ui_buffer->input_offset.row, current_ui_buffer->input_offset.col, header_span,
                    &zsvsheet_opts, custom_prop_handler)) {
        zsvsheet_priv_set_status(&display_dims, 1, "Unexpected error!"); // to do: better error message
        continue;
      }
    }

    display_buffer_subtable(ub, header_span, &display_dims);
  }

  endwin();
  free(handler_state.find);
  free(handler_state.goto_column);
zsvsheet_exit:
  zsvsheet_ui_buffers_delete(current_ui_buffer);
  zsvsheet_key_handlers_delete(&zsvsheet_key_handlers, &zsvsheet_next_key_handler);
  return err;
}

const char *display_cell(struct zsvsheet_screen_buffer *buff, size_t data_row, size_t data_col, int row, int col,
                         size_t cell_display_width) {
  char *str = (char *)zsvsheet_screen_buffer_cell_display(buff, data_row, data_col);
  size_t len = str ? strlen(str) : 0;
  int attrs = zsvsheet_screen_buffer_cell_attrs(buff, data_row, data_col);
  if (attrs)
    attron(attrs);
  if (len == 0 || has_multibyte_char(str, len < cell_display_width ? len : cell_display_width) == 0)
    mvprintw(row, col * cell_display_width, "%-*.*s", (int)cell_display_width, (int)cell_display_width - 1, str);
  else {
    size_t used_width;
    int err = 0;
    unsigned char *s = (unsigned char *)str;
    size_t nbytes = utf8_bytes_up_to_max_width_and_replace_newlines(s, len, cell_display_width - 2, &used_width, &err);

    // convert the substring to wide characters
    wchar_t wsubstring[256]; // Ensure this buffer is large enough
#if defined(WIN32) || defined(_WIN32)
    // windows does not have mbsnrtowcs
    char *p = (char *)str;
    char tmp_ch = p[nbytes];
    p[nbytes] = '\0';
    size_t wlen = mbstowcs(wsubstring, p, sizeof(wsubstring) / sizeof(wchar_t));
    p[nbytes] = tmp_ch;
#else
    const char *p = str;
    size_t wlen = mbsnrtowcs(wsubstring, &p, nbytes, sizeof(wsubstring) / sizeof(wchar_t), NULL);
#endif
    if (wlen == (size_t)-1) {
      fprintf(stderr, "Unable to convert to wide chars: %s\n", str);
      goto out;
    }

    // move to the desired position
    move(row, col * cell_display_width);

    // print the wide-character string with right padding
    addnwstr(wsubstring, wlen);
    for (size_t k = used_width; k < cell_display_width; k++)
      addch(' ');
  }
out:
  if (attrs)
    attroff(attrs);
  return str;
}

static size_t zsvsheet_max_buffer_cols(struct zsvsheet_ui_buffer *ui_buffer) {
  size_t col_count = ui_buffer->dimensions.col_count + (ui_buffer->rownum_col_offset ? 1 : 0);
  return col_count > zsvsheet_screen_buffer_cols(ui_buffer->buffer) ? zsvsheet_screen_buffer_cols(ui_buffer->buffer)
                                                                    : col_count;
}

static void display_buffer_subtable(struct zsvsheet_ui_buffer *ui_buffer, size_t input_header_span,
                                    struct zsvsheet_display_dimensions *ddims) {
  struct zsvsheet_screen_buffer *buffer = ui_buffer->buffer;
  size_t start_row = ui_buffer->buff_offset.row;
  size_t buffer_used_row_count = ui_buffer->buff_used_rows;
  size_t start_col = ui_buffer->buff_offset.col;
  size_t max_col_count = zsvsheet_max_buffer_cols(ui_buffer);
  size_t cursor_row = ui_buffer->cursor_row;
  size_t cursor_col = ui_buffer->cursor_col;

  erase(); // use erase() instead of clear() to avoid changes being saved to
           // screen buffer history
  const size_t DATA_START_ROW = input_header_span;
  const size_t SCREEN_DATA_START_ROW = ddims->header_span;

  size_t SCREEN_DATA_ROWS = ddims->rows > SCREEN_DATA_START_ROW + 1 ? ddims->rows - SCREEN_DATA_START_ROW - 1 : 2;

  size_t end_row = start_row + SCREEN_DATA_ROWS + DATA_START_ROW;
  if (end_row > buffer_used_row_count)
    end_row = buffer_used_row_count;

  size_t cell_display_width = zsvsheet_cell_display_width(ui_buffer, ddims);
  size_t end_col = start_col + ddims->columns / cell_display_width;
  if (end_col > max_col_count)
    end_col = max_col_count;

  const char *cursor_value = NULL;
  // First, display header row (buffer[0]) at screen row 0
  attron(A_REVERSE);
  for (size_t j = start_col; j < end_col; j++) {
    if (cursor_row == 0 && cursor_col + start_col == j) {
      attroff(A_REVERSE);
      cursor_value = display_cell(buffer, 0, j, 0, j - start_col, cell_display_width);
      attron(A_REVERSE);
    } else
      display_cell(buffer, 0, j, 0, j - start_col, cell_display_width);
  }
  attroff(A_REVERSE);

  // Then display data starting from start_row + DATA_START_ROW
  size_t screen_row = SCREEN_DATA_START_ROW;
  for (size_t i = start_row + DATA_START_ROW; i < end_row; i++, screen_row++) {
    for (size_t j = start_col; j < end_col; j++) {
      if (screen_row == cursor_row && j == cursor_col + start_col) {
        attron(A_REVERSE);
        cursor_value = display_cell(buffer, i, j, screen_row, j - start_col, cell_display_width);
        attroff(A_REVERSE);
      } else {
        display_cell(buffer, i, j, screen_row, j - start_col, cell_display_width);
      }
    }
  }

  if (ui_buffer->parse_errs.count > 0)
    zsvsheet_priv_set_status(ddims, 0, "? for help, :errors for errors");
  else
    zsvsheet_priv_set_status(ddims, 0, "? for help");

  if (cursor_value)
    mvprintw(ddims->rows - ddims->footer_span, strlen(zsvsheet_status_text), "%s", cursor_value);
  refresh();
}
