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

#if defined(WIN32) || defined(_WIN32)
#include <ncurses/ncurses.h>
#else
#if __has_include(<curses.h>)
#include <curses.h>
#elif __has_include(<ncursesw/curses.h>)
#include <ncursesw/curses.h>
#endif
#endif

#include <locale.h>
#include <wchar.h>
#ifdef ZSVSHEET_USE_THREADS
#include <pthread.h>
#endif

#define ZSV_COMMAND sheet
#include "zsv_command.h"

#include "../include/zsv/ext/sheet.h"
#include "sheet/sheet_internal.h"
#include "sheet/buffer.h"
#include "sheet/buffer.c"

#define ZSVSHEET_CELL_DISPLAY_WIDTH 10

struct zsvsheet_opts {
  char hide_row_nums; // if 1, will load row nums
  const char *find;
  size_t found_rownum;
};

#include "sheet/utf8-width.c"
#include "sheet/ui_buffer.c"
#include "sheet/read-data.c"
#include "sheet/key-bindings.c"

void display_buffer_subtable(struct zsvsheet_ui_buffer *ui_buffer, size_t rownum_col_offset, size_t input_header_span,
                             struct zsvsheet_display_dimensions *ddims);

void zsvsheet_set_status(const struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt, ...);

void get_subcommand(const char *prompt, char *buff, size_t buffsize, int footer_row) {
  *buff = '\0';
  // this is a hack to blank-out the currently-selected cell value
  int max_screen_width = 256; // to do: don't assume this
  for (int i = 0; i < max_screen_width; i++)
    mvprintw(footer_row, i, "%c", ' ');

  mvprintw(footer_row, 0, "%s: ", prompt);

  int ch;
  size_t idx = 0;
  int y, x;
  getyx(stdscr, y, x); // Get the current cursor position after the prompt
  while (1) {
    ch = getch(); // Read a character from the user
    if (ch == 27) {
      buff[0] = '\0';                                            // Clear the buffer or handle as needed
      break;                                                     // Exit the loop
    } else if (ch == '\n' || ch == '\r') {                       // ENTER key
      buff[idx] = '\0';                                          // Null-terminate the string
      break;                                                     // Exit the loop
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

size_t zsvsheet_get_input_raw_row(struct zsvsheet_rowcol *input_offset, struct zsvsheet_rowcol *buff_offset,
                                  size_t cursor_row) {
  return input_offset->row + buff_offset->row + cursor_row;
}

#include "sheet/cursor.c"

// zsvsheet_handle_find_next: return non-zero if a result was found
char zsvsheet_handle_find_next(struct zsvsheet_ui_buffer *uib, const char *needle, struct zsvsheet_opts *zsvsheet_opts,
                               size_t header_span, struct zsvsheet_display_dimensions *ddims, int *update_buffer,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (zsvsheet_find_next(uib, needle, zsvsheet_opts, header_span, custom_prop_handler, opts_used) > 0) {
    *update_buffer = zsvsheet_goto_input_raw_row(uib, zsvsheet_opts->found_rownum, header_span, ddims, (size_t)-1);
    return 1;
  }
  zsvsheet_set_status(ddims, 1, "Not found");
  return 0;
}

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

void zsvsheet_set_status(const struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt, ...) {
  if (overwrite || !*zsvsheet_status_text) {
    va_list argv;
    va_start(argv, fmt);
    vsnprintf(zsvsheet_status_text, sizeof(zsvsheet_status_text), fmt, argv);
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

enum zsvsheet_status zsvsheet_key_handler(struct zsvsheet_key_handler_data *khd, int ch, char *cmdbuff,
                                          size_t cmdbuff_sz, // subcommand buffer
                                          struct zsvsheet_ui_buffer **base_ui_buffer,
                                          struct zsvsheet_ui_buffer **current_ui_buffer,
                                          const struct zsvsheet_display_dimensions *display_dims,
                                          struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  struct zsvsheet_subcommand_handler_context sctx = {0};
  sctx.ch = ch;
  sctx.ui_buffers = *base_ui_buffer;
  sctx.current_ui_buffer = *current_ui_buffer;
  zsvsheet_handler_status status = khd->subcommand_handler(&sctx);
  if (status == zsvsheet_handler_status_ok) {
    get_subcommand(sctx.prompt, cmdbuff, cmdbuff_sz, (int)(display_dims->rows - display_dims->footer_span));
    if (*cmdbuff == '\0')
      return zsvsheet_status_continue;
    struct zsvsheet_handler_context ctx = {.subcommand_value = cmdbuff,
                                           .ch = ch,
                                           .ui_buffers = {.base = base_ui_buffer, .current = current_ui_buffer},
                                           .display_dims = display_dims,
                                           .custom_prop_handler = custom_prop_handler,
                                           .opts_used = opts_used};
    status = khd->handler(&ctx);
  }
  if (status == zsvsheet_handler_status_ok)
    return zsvsheet_status_ok;
  if (status == zsvsheet_handler_status_ignore)
    return zsvsheet_status_continue;
  return zsvsheet_status_error;
}

struct zsvsheet_key_handler_data *zsvsheet_key_handlers = NULL;
struct zsvsheet_key_handler_data **zsvsheet_next_key_handler = &zsvsheet_key_handlers;

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsvsheet_usage();
    return zsv_status_ok;
  }

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

  char *find = NULL;
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
    if ((err = zsvsheet_ui_buffer_open_file(filename, optsp, NULL, custom_prop_handler, opts_used, &ui_buffers,
                                            &current_ui_buffer))) {
      if (err > 0)
        perror(filename);
      else
        fprintf(stderr, "%s: no data found", filename); // to do: change this to a base-buff status msg
      return -1;
    }
  }

  header_span = 1;
  initscr();
  noecho();
  keypad(stdscr, TRUE);
  cbreak();
  struct zsvsheet_display_dimensions display_dims = get_display_dimensions(1, 1);
  int zsvsheetch;
  size_t rownum_col_offset = 1;
  display_buffer_subtable(current_ui_buffer, rownum_col_offset, header_span, &display_dims);
  char cmdbuff[256]; // subcommand buffer

  // register built-in key handlers
  zsvsheet_register_command_internal(zsvsheet_key_open_file, "open-file", zsvsheet_file_subcommand_handler,
                                     zsvsheet_file_handler);
  zsvsheet_register_command_internal(zsvsheet_key_filter, "filter", zsvsheet_file_subcommand_handler,
                                     zsvsheet_file_handler);

  int ch;
  while ((zsvsheetch = zsvsheet_key_binding((ch = getch()))) != zsvsheet_key_quit) {
    zsvsheet_set_status(&display_dims, 1, "");
    int update_buffer = 0;
    switch (zsvsheetch) {
    case zsvsheet_key_resize:
      display_dims = get_display_dimensions(1, 1);
      break;
    case zsvsheet_key_move_top:
      update_buffer =
        zsvsheet_goto_input_raw_row(current_ui_buffer, 1, header_span, &display_dims, display_dims.header_span);
      break;
    case zsvsheet_key_move_bottom:
      if (current_ui_buffer->dimensions.row_count == 0)
        continue;
      if (current_ui_buffer->dimensions.row_count <= display_dims.rows - display_dims.footer_span)
        current_ui_buffer->cursor_row = current_ui_buffer->dimensions.row_count - 1;
      else {
        update_buffer =
          zsvsheet_goto_input_raw_row(current_ui_buffer, current_ui_buffer->dimensions.row_count - 1, header_span,
                                      &display_dims, display_dims.rows - display_dims.header_span - 1);
      }
      break;
    case zsvsheet_key_move_first_col:
      current_ui_buffer->cursor_col = 0;
      current_ui_buffer->buff_offset.col = 0;
      break;
    case zsvsheet_key_pg_up:
      if (current_ui_buffer->dimensions.row_count <= header_span)
        continue; // no data
      else {
        size_t current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                                    current_ui_buffer->cursor_row);
        if (current <= display_data_rowcount(&display_dims) + header_span)
          continue; // already at top
        else {
          size_t target = current - display_data_rowcount(&display_dims);
          if (target >= current_ui_buffer->dimensions.row_count)
            target = current_ui_buffer->dimensions.row_count > 0 ? current_ui_buffer->dimensions.row_count - 1 : 0;
          update_buffer = zsvsheet_goto_input_raw_row(current_ui_buffer, target, header_span, &display_dims,
                                                      current_ui_buffer->cursor_row);
        }
      }
      break;
    case zsvsheet_key_pg_down: {
      size_t current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                                  current_ui_buffer->cursor_row);
      if (current >= current_ui_buffer->dimensions.row_count - display_data_rowcount(&display_dims))
        continue; // already at bottom
      else {
        size_t target = current + display_data_rowcount(&display_dims);
        update_buffer = zsvsheet_goto_input_raw_row(current_ui_buffer, target, header_span, &display_dims,
                                                    current_ui_buffer->cursor_row);
      }
    } break;
    case zsvsheet_key_move_last_col:
      // to do: directly set current_ui_buffer->cursor_col and buff_offset.col
      while (cursor_right(display_dims.columns, ZSVSHEET_CELL_DISPLAY_WIDTH,
                          current_ui_buffer->dimensions.col_count + rownum_col_offset >
                              zsvsheet_buffer_cols(current_ui_buffer->buffer)
                            ? zsvsheet_buffer_cols(current_ui_buffer->buffer)
                            : current_ui_buffer->dimensions.col_count + rownum_col_offset,
                          &current_ui_buffer->cursor_col, &current_ui_buffer->buff_offset.col) > 0)
        ;
      break;
    case zsvsheet_key_move_up: {
      size_t current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                                  current_ui_buffer->cursor_row);
      if (current > header_span) {
        update_buffer =
          zsvsheet_goto_input_raw_row(current_ui_buffer, current - 1, header_span, &display_dims,
                                      current_ui_buffer->cursor_row > 0 ? current_ui_buffer->cursor_row - 1 : 0);
      } else if (current_ui_buffer->cursor_row > 0) {
        current_ui_buffer->cursor_row--;
      }
    } break;
    case zsvsheet_key_move_down: {
      size_t current = zsvsheet_get_input_raw_row(&current_ui_buffer->input_offset, &current_ui_buffer->buff_offset,
                                                  current_ui_buffer->cursor_row);
      if (current >= current_ui_buffer->dimensions.row_count - 1)
        continue; // already at bottom
      update_buffer = zsvsheet_goto_input_raw_row(current_ui_buffer, current + 1, header_span, &display_dims,
                                                  current_ui_buffer->cursor_row + 1);
    } break;
    case zsvsheet_key_move_left:
      if (current_ui_buffer->cursor_col > 0) {
        current_ui_buffer->cursor_col--;
      } else if (current_ui_buffer->buff_offset.col > 0) {
        current_ui_buffer->buff_offset.col--;
      }
      break;
    case zsvsheet_key_move_right:
      cursor_right(display_dims.columns, ZSVSHEET_CELL_DISPLAY_WIDTH,
                   current_ui_buffer->dimensions.col_count + rownum_col_offset >
                       zsvsheet_buffer_cols(current_ui_buffer->buffer)
                     ? zsvsheet_buffer_cols(current_ui_buffer->buffer)
                     : current_ui_buffer->dimensions.col_count + rownum_col_offset,
                   &current_ui_buffer->cursor_col, &current_ui_buffer->buff_offset.col);
      break;
    case zsvsheet_key_escape:         // escape
      if (current_ui_buffer->prior) { // current_ui_buffer is not the base/blank buffer
        if (zsvsheet_ui_buffer_pop(&ui_buffers, &current_ui_buffer, NULL)) {
          update_buffer = 1;
          break;
        }
      }
      continue;
    case zsvsheet_key_find_next:
      if (find) {
        struct zsvsheet_opts zsvsheet_opts = {0};
        if (!zsvsheet_handle_find_next(current_ui_buffer, find, &zsvsheet_opts, header_span, &display_dims,
                                       &update_buffer, custom_prop_handler, opts_used))
          continue;
      }
      break;
    case zsvsheet_key_find:
      get_subcommand("Find", cmdbuff, sizeof(cmdbuff), (int)(display_dims.rows - display_dims.footer_span));
      if (*cmdbuff != '\0') {
        free(find);
        find = strdup(cmdbuff);
        struct zsvsheet_opts zsvsheet_opts = {0};
        if (!zsvsheet_handle_find_next(current_ui_buffer, find, &zsvsheet_opts, header_span, &display_dims,
                                       &update_buffer, custom_prop_handler, opts_used))
          continue;
      }
      break;
    case zsvsheet_key_open_file:
    case zsvsheet_key_filter: {
      struct zsvsheet_key_handler_data *zkhd = zsvsheet_get_registered_key_handler(ch, NULL, zsvsheet_key_handlers);
      if (!zkhd || zsvsheet_key_handler(zkhd, ch, cmdbuff, sizeof(cmdbuff), &ui_buffers, &current_ui_buffer,
                                        &display_dims, custom_prop_handler, opts_used) == zsvsheet_status_continue)
        continue;
      break;
    }
    default: {
      struct zsvsheet_key_handler_data *zkhd = zsvsheet_get_registered_key_handler(ch, NULL, zsvsheet_key_handlers);
      if (zkhd && zsvsheet_key_handler(zkhd, ch, cmdbuff, sizeof(cmdbuff), &ui_buffers, &current_ui_buffer,
                                       &display_dims, custom_prop_handler, opts_used) != zsvsheet_status_continue)
        break;
      continue;
    }
    }
    if (update_buffer && current_ui_buffer->filename) {
      struct zsvsheet_opts zsvsheet_opts = {0};
      if (read_data(&current_ui_buffer, NULL, current_ui_buffer->input_offset.row, current_ui_buffer->input_offset.col,
                    header_span, current_ui_buffer->dimensions.index, &zsvsheet_opts, custom_prop_handler, opts_used)) {
        zsvsheet_set_status(&display_dims, 1, "Unexpected error!"); // to do: better error message
        continue;
      }
    }
    if (current_ui_buffer->status)
      zsvsheet_set_status(&display_dims, 1, current_ui_buffer->status);
    display_buffer_subtable(current_ui_buffer, rownum_col_offset, header_span, &display_dims);
  }

  endwin();
  free(find);
  zsvsheet_ui_buffers_delete(ui_buffers);
  zsvsheet_key_handlers_delete(&zsvsheet_key_handlers, &zsvsheet_next_key_handler);
  return 0;
}

const char *display_cell(struct zsvsheet_buffer *buff, size_t data_row, size_t data_col, int row, int col) {
  char *str = (char *)zsvsheet_buffer_cell_display(buff, data_row, data_col);
  size_t len = str ? strlen(str) : 0;
  if (len == 0 || has_multibyte_char(str, len < ZSVSHEET_CELL_DISPLAY_WIDTH ? len : ZSVSHEET_CELL_DISPLAY_WIDTH) == 0)
    mvprintw(row, col * ZSVSHEET_CELL_DISPLAY_WIDTH, "%-*.*s", ZSVSHEET_CELL_DISPLAY_WIDTH,
             ZSVSHEET_CELL_DISPLAY_WIDTH - 1, str);
  else {
    size_t used_width;
    int err = 0;
    unsigned char *s = (unsigned char *)str;
    size_t nbytes =
      utf8_bytes_up_to_max_width_and_replace_newlines(s, len, ZSVSHEET_CELL_DISPLAY_WIDTH - 2, &used_width, &err);

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
      return str;
    }

    // move to the desired position
    move(row, col * ZSVSHEET_CELL_DISPLAY_WIDTH);

    // print the wide-character string with right padding
    addnwstr(wsubstring, wlen);
    for (size_t k = used_width; k < ZSVSHEET_CELL_DISPLAY_WIDTH; k++)
      addch(' ');
  }
  return str;
}

void display_buffer_subtable(struct zsvsheet_ui_buffer *ui_buffer, size_t rownum_col_offset, size_t input_header_span,
                             struct zsvsheet_display_dimensions *ddims) {
  struct zsvsheet_buffer *buffer = ui_buffer->buffer;
  size_t start_row = ui_buffer->buff_offset.row;
  size_t buffer_used_row_count = ui_buffer->buff_used_rows;
  size_t start_col = ui_buffer->buff_offset.col;
  size_t max_col_count = ui_buffer->dimensions.col_count + rownum_col_offset > zsvsheet_buffer_cols(ui_buffer->buffer)
                           ? zsvsheet_buffer_cols(ui_buffer->buffer)
                           : ui_buffer->dimensions.col_count + rownum_col_offset;
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

  size_t end_col = start_col + ddims->columns / ZSVSHEET_CELL_DISPLAY_WIDTH;
  if (end_col > max_col_count)
    end_col = max_col_count;

  const char *cursor_value = NULL;
  // First, display header row (buffer[0]) at screen row 0
  attron(A_REVERSE);
  for (size_t j = start_col; j < end_col; j++) {
    if (cursor_row == 0 && cursor_col + start_col == j) {
      attroff(A_REVERSE);
      cursor_value = display_cell(buffer, 0, j, 0, j - start_col);
      attron(A_REVERSE);
    } else
      display_cell(buffer, 0, j, 0, j - start_col);
  }
  attroff(A_REVERSE);

  // Then display data starting from start_row + DATA_START_ROW
  size_t screen_row = SCREEN_DATA_START_ROW;
  for (size_t i = start_row + DATA_START_ROW; i < end_row; i++, screen_row++) {
    for (size_t j = start_col; j < end_col; j++) {
      if (screen_row == cursor_row && j == cursor_col + start_col) {
        attron(A_REVERSE);
        cursor_value = display_cell(buffer, i, j, screen_row, j - start_col);
        attroff(A_REVERSE);
      } else {
        display_cell(buffer, i, j, screen_row, j - start_col);
      }
    }
  }

  zsvsheet_set_status(ddims, 0, "? for help ");
  if (cursor_value)
    mvprintw(ddims->rows - ddims->footer_span, strlen(zsvsheet_status_text), "%s", cursor_value);
  refresh();
}
