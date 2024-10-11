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

#include <zsv.h>
#include "sheet/sheet_internal.h"

#if defined(WIN32) || defined(_WIN32)
#include <ncurses/ncurses.h>
#else
// #ifdef HAVE_NCURSES
#if __has_include(<curses.h>)
#include <curses.h>
#elif __has_include(<ncursesw/curses.h>)
//  #else
#include <ncursesw/curses.h>
#endif
#endif

#include <locale.h>
#include <wchar.h>
#ifdef ZSVSHEET_USE_THREADS
#include <pthread.h>
#endif

#include "sheet/buffer.h"
#include "sheet/buffer.c"

#define ZSVSHEET_CELL_DISPLAY_WIDTH 10

struct zsvsheet_opts {
  char hide_row_nums; // if 1, will load row nums
  const char *find;
  size_t found_rownum;
};

void display_buffer_subtable(
  struct zsvsheet_buffer *buffer,
  size_t start_row, size_t buffer_used_row_count, size_t start_col, size_t max_col_count, size_t cursor_row,
  size_t cursor_col, size_t input_header_span, struct zsvsheet_display_dimensions *ddims);

#include "sheet/utf8-width.c"
#include "sheet/read-data.c"
#include "sheet/key-bindings.c"

void zsvsheet_set_status(struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt, ...);

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
char zsvsheet_handle_find_next(zsvsheet_buffer_t buffer, const char *filename, const char *row_filter,
                               const char *needle, struct zsv_opts *zsv_opts, struct zsvsheet_opts *zsvsheet_opts,
                               size_t header_span, struct zsvsheet_rowcol *input_offset,
                               struct zsvsheet_rowcol *buff_offset, struct zsvsheet_input_dimensions *input_dims,
                               size_t *cursor_rowp, struct zsvsheet_display_dimensions *ddims, int *update_buffer,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (zsvsheet_find_next(filename, row_filter, needle, zsv_opts, zsvsheet_opts, header_span, input_offset, buff_offset,
                         *cursor_rowp, input_dims, custom_prop_handler, opts_used) > 0) {
    *update_buffer = zsvsheet_goto_input_raw_row(buffer, zsvsheet_opts->found_rownum, header_span, input_offset,
                                                 buff_offset, input_dims, cursor_rowp, ddims, (size_t)-1);
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
void zsvsheet_set_status(struct zsvsheet_display_dimensions *ddims, int overwrite, const char *fmt, ...) {
  if (overwrite || !*zsvsheet_status_text) {
    va_list argv;
    va_start(argv, fmt);
    vsnprintf(zsvsheet_status_text, sizeof(zsvsheet_status_text), fmt, argv);
    va_end(argv);
    // note: if (n < (int)sizeof(zsvsheet_status_text)), then we just ignore
  }
  // clear the entire line
  mvprintw(ddims->rows - ddims->footer_span, 0, "%-*s", (int)sizeof(zsvsheet_status_text), "");

  // add status, highlighted
  attron(A_REVERSE);
  mvprintw(ddims->rows - ddims->footer_span, 0, "%s", zsvsheet_status_text);
  attroff(A_REVERSE);
}

#include "sheet/terminfo.c"

#define ZSV_COMMAND sheet
#include "zsv_command.h"
#include "sheet/usage.c"

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsvsheet_usage();
    return zsv_status_ok;
  }

  if (argc < 2) {
    fprintf(stderr, "Please specify an input file\n");
    return 1;
  }

  const char *locale = setlocale(LC_ALL, "C.UTF-8");
  if (!locale || strstr(locale, "UTF-8") == NULL)
    locale = setlocale(LC_ALL, "");
  if (!locale || strstr(locale, "UTF-8") == NULL) {
    fprintf(stderr, "Unable to set locale to UTF-8\n");
    return 1; // TO DO: option to continue
  }

  if (!terminfo_ok()) {
    fprintf(stderr, "Warning: unable to set or detect TERMINFO\n");
    return 1;
  }

  const char *filename = argv[1];
  char *row_filter = NULL;
  char *find = NULL;
  struct zsvsheet_input_dimensions file_dimensions = {0};
  struct zsvsheet_input_dimensions input_dimensions = {0};
  struct zsvsheet_input_dimensions filter_dimensions = {0};
#ifdef ZSVSHEET_USE_THREADS
  pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t filter_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
  struct zsv_opts opts = *optsp;

  // input_offset: location within the input from which the buffer is read
  // i.e. if row = 5, col = 3, the buffer data starts from cell D6
  struct zsvsheet_rowcol input_offset = {0};

  // buff_offset: location within the buffer from which the data is
  // displayed on the screen
  struct zsvsheet_rowcol buff_offset = {0};

  size_t buff_used_rows = 0;
  size_t header_span = 0; // number of rows that comprise the header
  struct zsvsheet_opts zsvsheet_opts = {0};
  int err;
  zsvsheet_buffer_t buffer = NULL;
  struct zsvsheet_buffer_opts bopts = {0};
  if ((err = read_data(&buffer, &bopts, filename, &opts, &file_dimensions.col_count, NULL, 0, 0, 0, NULL,
                       &zsvsheet_opts, custom_prop_handler, opts_used, &buff_used_rows)) != 0 ||
      !buff_used_rows) {
    if (err)
      perror(filename);
    else
      fprintf(stderr, "%s: no data found", filename);
    zsvsheet_buffer_delete(buffer);
    return -1;
  }

  header_span = 1;
  initscr();
  noecho();
  keypad(stdscr, TRUE);
  cbreak();
  struct zsvsheet_display_dimensions display_dims = get_display_dimensions(1, 1);

  get_data_index_async(&file_dimensions.index, filename, &opts, NULL, &file_dimensions.row_count, custom_prop_handler,
                       opts_used
#ifdef ZSVSHEET_USE_THREADS
                       ,
                       &input_mutex
#endif
  );
  memcpy(&input_dimensions, &file_dimensions, sizeof(input_dimensions));
  int zsvsheetch;
  size_t cursor_row = 1; // first row is header
  size_t cursor_col = 0;
  char *help_suffix = NULL;
  size_t rownum_col_offset = 1;
  display_buffer_subtable(buffer, buff_offset.row, buff_used_rows, buff_offset.col,
                          input_dimensions.col_count + rownum_col_offset > zsvsheet_buffer_cols(buffer)
                            ? zsvsheet_buffer_cols(buffer)
                            : input_dimensions.col_count + rownum_col_offset,
                          cursor_row, cursor_col, header_span, &display_dims);

  char cmdbuff[256]; // subcommand buffer

  while ((zsvsheetch = zsvsheet_key_binding(getch())) != zsvsheet_key_quit) {
    zsvsheet_set_status(&display_dims, 1, "");
    int update_buffer = 0;
    switch (zsvsheetch) {
    case zsvsheet_key_move_top:
      update_buffer =
        zsvsheet_goto_input_raw_row(buffer, 1, header_span, &input_offset, &buff_offset, &input_dimensions, &cursor_row,
                                    &display_dims, display_dims.header_span);
      break;
    case zsvsheet_key_move_bottom:
      if (input_dimensions.row_count == 0)
        continue;
      if (input_dimensions.row_count <= display_dims.rows - display_dims.footer_span)
        cursor_row = input_dimensions.row_count - 1;
      else {
        update_buffer = zsvsheet_goto_input_raw_row(buffer, input_dimensions.row_count - 1, header_span, &input_offset,
                                                    &buff_offset, &input_dimensions, &cursor_row, &display_dims,
                                                    display_dims.rows - display_dims.header_span - 1);
      }
      break;
    case zsvsheet_key_move_first_col:
      cursor_col = 0;
      buff_offset.col = 0;
      break;
    case zsvsheet_key_pg_up:
      if (input_dimensions.row_count <= header_span)
        continue; // no data
      else {
        size_t current = zsvsheet_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
        if (current <= display_data_rowcount(&display_dims) + header_span)
          continue; // already at top
        else {
          size_t target = current - display_data_rowcount(&display_dims);
          if (target >= input_dimensions.row_count)
            target = input_dimensions.row_count > 0 ? input_dimensions.row_count - 1 : 0;
          update_buffer = zsvsheet_goto_input_raw_row(buffer, target, header_span, &input_offset, &buff_offset,
                                                      &input_dimensions, &cursor_row, &display_dims, cursor_row);
        }
      }
      break;
    case zsvsheet_key_pg_down: {
      size_t current = zsvsheet_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current >= input_dimensions.row_count - display_data_rowcount(&display_dims))
        continue; // already at bottom
      else {
        size_t target = current + display_data_rowcount(&display_dims);
        update_buffer = zsvsheet_goto_input_raw_row(buffer, target, header_span, &input_offset, &buff_offset,
                                                    &input_dimensions, &cursor_row, &display_dims, cursor_row);
      }
    } break;
    case zsvsheet_key_move_last_col:
      // to do: directly set cursor_col and buff_offset.col
      while (cursor_right(display_dims.columns, ZSVSHEET_CELL_DISPLAY_WIDTH,
                          input_dimensions.col_count + rownum_col_offset > zsvsheet_buffer_cols(buffer)
                            ? zsvsheet_buffer_cols(buffer)
                            : input_dimensions.col_count + rownum_col_offset,
                          &cursor_col, &buff_offset.col) > 0)
        ;
      break;
    case zsvsheet_key_move_up: {
      size_t current = zsvsheet_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current > header_span) {
        update_buffer =
          zsvsheet_goto_input_raw_row(buffer, current - 1, header_span, &input_offset, &buff_offset, &input_dimensions,
                                      &cursor_row, &display_dims, cursor_row > 0 ? cursor_row - 1 : 0);
      } else if (cursor_row > 0) {
        cursor_row--;
      }
    } break;
    case zsvsheet_key_move_down: {
      size_t current = zsvsheet_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current >= input_dimensions.row_count - 1)
        continue; // already at bottom
      update_buffer = zsvsheet_goto_input_raw_row(buffer, current + 1, header_span, &input_offset, &buff_offset,
                                                  &input_dimensions, &cursor_row, &display_dims, cursor_row + 1);
    } break;
    case zsvsheet_key_move_left:
      if (cursor_col > 0) {
        cursor_col--;
      } else if (buff_offset.col > 0) {
        buff_offset.col--;
      }
      break;
    case zsvsheet_key_move_right:
      cursor_right(display_dims.columns, ZSVSHEET_CELL_DISPLAY_WIDTH,
                   input_dimensions.col_count + rownum_col_offset > zsvsheet_buffer_cols(buffer)
                     ? zsvsheet_buffer_cols(buffer)
                     : input_dimensions.col_count + rownum_col_offset,
                   &cursor_col, &buff_offset.col);
      break;
    case zsvsheet_key_escape: // escape
      if (row_filter) {
        // remove filter
        free(row_filter);
        row_filter = NULL;
        free(help_suffix);
        help_suffix = NULL;
        update_buffer = 1;
        // to do: restore old buff_offset.row and cursor_row
        break;
      }
      continue;
    case zsvsheet_key_find_next:
      if (find) {
        if (!zsvsheet_handle_find_next(buffer, filename, row_filter, find, &opts, &zsvsheet_opts, header_span,
                                       &input_offset, &buff_offset, &input_dimensions, &cursor_row, &display_dims,
                                       &update_buffer, custom_prop_handler, opts_used))
          continue;
      }
      break;
    case zsvsheet_key_find:
      get_subcommand("Find", cmdbuff, sizeof(cmdbuff), (int)(display_dims.rows - display_dims.footer_span));
      if (*cmdbuff != '\0') {
        free(find);
        find = strdup(cmdbuff);
        if (!zsvsheet_handle_find_next(buffer, filename, row_filter, find, &opts, &zsvsheet_opts, header_span,
                                       &input_offset, &buff_offset, &input_dimensions, &cursor_row, &display_dims,
                                       &update_buffer, custom_prop_handler, opts_used))
          continue;
      }
      break;
    case zsvsheet_key_filter:
      get_subcommand("Filter", cmdbuff, sizeof(cmdbuff), (int)(display_dims.rows - display_dims.footer_span));
      if (*cmdbuff != '\0') {
        row_filter = strdup(cmdbuff);
        if (row_filter && !*row_filter) {
          // empty string
          free(row_filter);
          row_filter = NULL;
        }
        if (row_filter) {
          size_t found = 0;
          if (read_data(&buffer, &bopts, filename, &opts, &filter_dimensions.col_count, row_filter, 0, 0, header_span,
                        NULL, &zsvsheet_opts, custom_prop_handler, opts_used, &found)) {
            filter_dimensions.row_count = 0;
            zsvsheet_set_status(&display_dims, 1, "Unexpected error!"); // to do: better error message
            continue;
          } else if (found > 1) {
            buff_used_rows = found;
            get_data_index_async(&filter_dimensions.index, filename, &opts, row_filter, &filter_dimensions.row_count,
                                 custom_prop_handler, opts_used
#ifdef ZSVSHEET_USE_THREADS
                                 ,
                                 &filter_mutex
#endif
            );
            memcpy(&input_dimensions, &filter_dimensions, sizeof(input_dimensions));
            input_offset.row = 0;
            buff_offset.row = 0;
            cursor_row = 1;
            // not sure why but using ncurses, erase() and refresh() needed for screen to properly redraw
            erase();
            refresh();
          } else {
            filter_dimensions.row_count = 0;
            zsvsheet_set_status(&display_dims, 1, "Not found");
            continue;
          }
        }
      }
      break;
    }
    if (update_buffer) {
      if (read_data(&buffer, &bopts, filename, &opts, NULL, row_filter, input_offset.row, input_offset.col, header_span,
                    row_filter ? filter_dimensions.index : file_dimensions.index, &zsvsheet_opts, custom_prop_handler,
                    opts_used, &buff_used_rows)) {
        zsvsheet_set_status(&display_dims, 1, "Unexpected error!"); // to do: better error message
        continue;
      } else if (row_filter)
        memcpy(&input_dimensions, &filter_dimensions, sizeof(input_dimensions));
      else
        memcpy(&input_dimensions, &file_dimensions, sizeof(input_dimensions));
    }
    if (filter_dimensions.row_count)
      zsvsheet_set_status(&display_dims, 1, "(%zu filtered rows) ", filter_dimensions.row_count - 1);
    display_buffer_subtable(buffer, buff_offset.row, buff_used_rows, buff_offset.col,
                            input_dimensions.col_count + rownum_col_offset > zsvsheet_buffer_cols(buffer)
                              ? zsvsheet_buffer_cols(buffer)
                              : input_dimensions.col_count + rownum_col_offset,
                            cursor_row, cursor_col, header_span, &display_dims);
  }

  endwin();
  free(help_suffix);
  free(row_filter);
  free(find);
  zsvsheet_buffer_delete(buffer);
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
    const char *p = str;
    size_t wlen = mbsnrtowcs(wsubstring, &p, nbytes, sizeof(wsubstring) / sizeof(wchar_t), NULL);
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

void display_buffer_subtable(struct zsvsheet_buffer *buffer, size_t start_row, size_t buffer_used_row_count,
                             size_t start_col, size_t max_col_count, size_t cursor_row, size_t cursor_col,
                             size_t input_header_span, struct zsvsheet_display_dimensions *ddims) {
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
