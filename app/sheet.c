#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include <zsv.h>
#include "sheet/sheet_internal.h"

#define _XOPEN_SOURCE_EXTENDED
#if defined(WIN32) || defined(_WIN32)
#include <ncurses/ncurses.h>
#else
#ifdef HAVE_NCURSES
#include <curses.h>
#else
#include <ncursesw/curses.h>
#endif
#endif

#include <locale.h>
#include <wchar.h>
#ifdef ZTV_USE_THREADS
#include <pthread.h>
#endif

#define ZTV_BUFFER_ROWS 1000
#define ZTV_MAX_COLS 100
#define ZTV_MAX_CELL_LEN 256 /* must be a multiple of 16 */
#define ZTV_CELL_DISPLAY_WIDTH 10

struct ztv_opts {
  char hide_row_nums; // if 1, will load row nums
  const char *find;
  size_t found_rownum;
};

void display_buffer_subtable(char buffer[ZTV_BUFFER_ROWS][ZTV_MAX_COLS][ZTV_MAX_CELL_LEN], size_t start_row,
                             size_t buffer_used_row_count, size_t start_col, size_t max_col_count, size_t cursor_row,
                             size_t cursor_col, size_t input_header_span, struct display_dims *ddims);

#include "sheet/utf8-width.c"
#include "sheet/read-data.c"
#include "sheet/key-bindings.c"

char ztv_status_text[256] = {0};
void ztv_set_status(struct display_dims *ddims, const char *fmt, ...);

void get_subcommand(const char *prompt, char *buff, size_t buffsize, int footer_row) {
  *buff = '\0';
  // this is a hack to blank-out the currently-selected cell value
  for (int i = 0; i < ZTV_MAX_CELL_LEN; i++)
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

size_t ztv_get_input_raw_row(struct ztv_rowcol *input_offset, struct ztv_rowcol *buff_offset, size_t cursor_row) {
  return input_offset->row + buff_offset->row + cursor_row;
}

#include "sheet/cursor.c"

char input_data[ZTV_BUFFER_ROWS][ZTV_MAX_COLS][ZTV_MAX_CELL_LEN] = {0};

// ztv_handle_find_next: return non-zero if a result was found
char ztv_handle_find_next(const char *filename, const char *row_filter, const char *needle, struct zsv_opts *zsv_opts,
                          struct ztv_opts *ztv_opts, size_t header_span, struct ztv_rowcol *input_offset,
                          struct ztv_rowcol *buff_offset, struct input_dimensions *input_dims, size_t *cursor_rowp,
                          struct display_dims *ddims, int *update_buffer, struct zsv_prop_handler *custom_prop_handler,
                          const char *opts_used) {
  if (ztv_find_next(filename, row_filter, needle, zsv_opts, ztv_opts, header_span, input_offset, buff_offset,
                    *cursor_rowp, input_dims, custom_prop_handler, opts_used) > 0) {
    *update_buffer = ztv_goto_input_raw_row(ztv_opts->found_rownum, header_span, input_offset, buff_offset, input_dims,
                                            cursor_rowp, ddims, (size_t)-1);
    return 1;
  }
  ztv_set_status(ddims, "Not found");
  return 0;
}

struct display_dims get_display_dimensions(size_t header_span, size_t footer_span) {
  struct display_dims dims = {0};
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

size_t display_data_rowcount(struct display_dims *dims) {
  return dims->rows - dims->footer_span - dims->header_span;
}

void ztv_set_status(struct display_dims *ddims, const char *fmt, ...) {
  va_list argv;
  va_start(argv, fmt);
  int n = vsnprintf(ztv_status_text, sizeof(ztv_status_text), fmt, argv);
  va_end(argv);
  if (n < (int)sizeof(ztv_status_text)) {
    // clear the entire line
    mvprintw(ddims->rows - ddims->footer_span, 0, "%-*s", sizeof(ztv_status_text), "");

    // add status, highlighted
    attron(A_REVERSE);
    mvprintw(ddims->rows - ddims->footer_span, 0, "%s", ztv_status_text);
    attroff(A_REVERSE);
  }
}

#include "sheet/terminfo.c"

#define ZSV_COMMAND sheet
#include "zsv_command.h"
#include "sheet/usage.c"

int ZSV_MAIN_FUNC(ZSV_COMMAND)(int argc, const char *argv[], struct zsv_opts *optsp,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    zsv_sheet_usage();
    return zsv_status_ok;
  }

  if (argc < 2) {
    fprintf(stderr, "Please specify an input file\n");
    return 1;
  }

  assert(ZTV_MAX_CELL_LEN > ZTV_CELL_DISPLAY_WIDTH);
  setlocale(LC_ALL, "");
  if (!terminfo_ok()) {
    fprintf(stderr, "Warning: unable to set or detect TERMINFO\n");
    return 1;
  }

  const char *filename = argv[1];
  char *row_filter = NULL;
  char *find = NULL;
  struct input_dimensions file_dimensions = {0};
  struct input_dimensions input_dimensions = {0};
  struct input_dimensions filter_dimensions = {0};
#ifdef ZTV_USE_THREADS
  pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t filter_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
  struct zsv_opts opts = *optsp;

  // input_offset: location within the input from which the buffer is read
  // i.e. if row = 5, col = 3, the buffer data starts from cell D6
  struct ztv_rowcol input_offset = {0};

  // buff_offset: location within the buffer from which the data is
  // displayed on the screen
  struct ztv_rowcol buff_offset = {0};

  size_t buff_used_rows = 0;
  size_t header_span = 0; // number of rows that comprise the header
  struct ztv_opts ztv_opts = {0};
  int err;
  if ((err = read_data(input_data, filename, &opts, &file_dimensions.col_count, NULL, 0, 0, 0, NULL, &ztv_opts,
                       custom_prop_handler, opts_used, &buff_used_rows)) != 0 ||
      !buff_used_rows) {
    if (err)
      perror(filename);
    else
      fprintf(stderr, "%s: no data found", filename);
    return -1;
  }

  header_span = 1;
  initscr();
  noecho();
  keypad(stdscr, TRUE);
  cbreak();
  struct display_dims display_dims = get_display_dimensions(1, 1);

  get_data_index_async(&file_dimensions.index, filename, &opts, NULL, &file_dimensions.row_count, custom_prop_handler,
                       opts_used
#ifdef ZTV_USE_THREADS
                       ,
                       &input_mutex
#endif
  );
  memcpy(&input_dimensions, &file_dimensions, sizeof(input_dimensions));
  int ztvch;
  size_t cursor_row = 1; // first row is header
  size_t cursor_col = 0;
  char *help_suffix = NULL;
  size_t rownum_col_offset = 1;
  display_buffer_subtable(input_data, buff_offset.row, buff_used_rows, buff_offset.col,
                          input_dimensions.col_count + rownum_col_offset > ZTV_MAX_COLS ? ZTV_MAX_COLS : input_dimensions.col_count + rownum_col_offset,
                          cursor_row, cursor_col, header_span, &display_dims);

  char cmdbuff[256]; // subcommand buffer

  while ((ztvch = ztv_key_binding(getch())) != ztv_key_quit) {
    ztv_set_status(&display_dims, "");
    int update_buffer = 0;
    switch (ztvch) {
    case ztv_key_move_top:
      update_buffer = ztv_goto_input_raw_row(1, header_span, &input_offset, &buff_offset, &input_dimensions,
                                             &cursor_row, &display_dims, display_dims.header_span);
      break;
    case ztv_key_move_bottom:
      if (input_dimensions.row_count == 0)
        continue;
      if (input_dimensions.row_count <= display_dims.rows - display_dims.footer_span)
        cursor_row = input_dimensions.row_count - 1;
      else {
        update_buffer = ztv_goto_input_raw_row(input_dimensions.row_count - 1, header_span, &input_offset, &buff_offset,
                                               &input_dimensions, &cursor_row, &display_dims,
                                               display_dims.rows - display_dims.header_span - 1);
      }
      break;
    case ztv_key_move_first_col:
      cursor_col = 0;
      buff_offset.col = 0;
      break;
    case ztv_key_pg_up:
      if (input_dimensions.row_count <= header_span)
        continue; // no data
      else {
        size_t current = ztv_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
        if (current <= display_data_rowcount(&display_dims) + header_span)
          continue; // already at top
        else {
          size_t target = current - display_data_rowcount(&display_dims);
          if (target >= input_dimensions.row_count)
            target = input_dimensions.row_count > 0 ? input_dimensions.row_count - 1 : 0;
          update_buffer = ztv_goto_input_raw_row(target, header_span, &input_offset, &buff_offset, &input_dimensions,
                                                 &cursor_row, &display_dims, cursor_row);
        }
      }
      break;
    case ztv_key_pg_down: {
      size_t current = ztv_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current >= input_dimensions.row_count - display_data_rowcount(&display_dims))
        continue; // already at bottom
      else {
        size_t target = current + display_data_rowcount(&display_dims);
        update_buffer = ztv_goto_input_raw_row(target, header_span, &input_offset, &buff_offset, &input_dimensions,
                                               &cursor_row, &display_dims, cursor_row);
      }
    } break;
    case ztv_key_move_last_col:
      // to do: directly set cursor_col and buff_offset.col
      while (cursor_right(display_dims.columns, ZTV_CELL_DISPLAY_WIDTH,
                          input_dimensions.col_count > ZTV_MAX_COLS ? ZTV_MAX_COLS : input_dimensions.col_count,
                          &cursor_col, &buff_offset.col) > 0)
        ;
      break;
    case ztv_key_move_up: {
      size_t current = ztv_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current > header_span) {
        update_buffer = ztv_goto_input_raw_row(current - 1, header_span, &input_offset, &buff_offset, &input_dimensions,
                                               &cursor_row, &display_dims, cursor_row > 0 ? cursor_row - 1 : 0);
      } else if (cursor_row > 0) {
        cursor_row--;
      }
    } break;
    case ztv_key_move_down: {
      size_t current = ztv_get_input_raw_row(&input_offset, &buff_offset, cursor_row);
      if (current >= input_dimensions.row_count - 1)
        continue; // already at bottom
      update_buffer = ztv_goto_input_raw_row(current + 1, header_span, &input_offset, &buff_offset, &input_dimensions,
                                             &cursor_row, &display_dims, cursor_row + 1);
    } break;
    case ztv_key_move_left:
      if (cursor_col > 0) {
        cursor_col--;
      } else if (buff_offset.col > 0) {
        buff_offset.col--;
      }
      break;
    case ztv_key_move_right:
      cursor_right(display_dims.columns, ZTV_CELL_DISPLAY_WIDTH,
                   input_dimensions.col_count > ZTV_MAX_COLS ? ZTV_MAX_COLS : input_dimensions.col_count, &cursor_col,
                   &buff_offset.col);
      break;
    case ztv_key_escape: // escape
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
    case ztv_key_find_next:
      if (find) {
        if (!ztv_handle_find_next(filename, row_filter, find, &opts, &ztv_opts, header_span, &input_offset,
                                  &buff_offset, &input_dimensions, &cursor_row, &display_dims, &update_buffer,
                                  custom_prop_handler, opts_used))
          continue;
      }
      break;
    case ztv_key_find:
      get_subcommand("Find", cmdbuff, sizeof(cmdbuff), (int)(display_dims.rows - display_dims.footer_span));
      if (*cmdbuff != '\0') {
        free(find);
        find = strdup(cmdbuff);
        if (!ztv_handle_find_next(filename, row_filter, find, &opts, &ztv_opts, header_span, &input_offset,
                                  &buff_offset, &input_dimensions, &cursor_row, &display_dims, &update_buffer,
                                  custom_prop_handler, opts_used))
          continue;
      }
      break;
    case ztv_key_filter:
      get_subcommand("Filter", cmdbuff, sizeof(cmdbuff), (int)(display_dims.rows - display_dims.footer_span));
      if (*cmdbuff != '\0') {
        row_filter = strdup(cmdbuff);
        if (row_filter && !*row_filter) {
          free(row_filter);
          row_filter = NULL;
        }
        if (row_filter) {
          size_t found = 0;
          if (read_data(input_data, filename, &opts, &filter_dimensions.col_count, row_filter, 0, 0, header_span, NULL,
                        &ztv_opts, custom_prop_handler, opts_used, &found)) {
            filter_dimensions.row_count = 0;
            ztv_set_status(&display_dims, "Unexpected error!"); // to do: better error message
            continue;
          } else if (found > 1) {
            buff_used_rows = found;
            get_data_index_async(&filter_dimensions.index, filename, &opts, row_filter, &filter_dimensions.row_count,
                                 custom_prop_handler, opts_used
#ifdef ZTV_USE_THREADS
                                 ,
                                 &filter_mutex
#endif
            );
            memcpy(&input_dimensions, &filter_dimensions, sizeof(input_dimensions));
            // input_offset.row = header_span;
            input_offset.row = 0;
            buff_offset.row = 0;
            cursor_row = 1;
          } else {
            filter_dimensions.row_count = 0;
            ztv_set_status(&display_dims, "Not found");
            continue;
          }
        }
      }
      break;
    }
    if (update_buffer) {
      if (read_data(input_data, filename, &opts, NULL, row_filter, input_offset.row, input_offset.col, header_span,
                    row_filter ? filter_dimensions.index : file_dimensions.index, &ztv_opts, custom_prop_handler,
                    opts_used, &buff_used_rows)) {
        ztv_set_status(&display_dims, "Unexpected error!"); // to do: better error message
        continue;
      } else if (row_filter)
        memcpy(&input_dimensions, &filter_dimensions, sizeof(input_dimensions));
      else
        memcpy(&input_dimensions, &file_dimensions, sizeof(input_dimensions));
    }
    if (filter_dimensions.row_count)
      ztv_set_status(&display_dims, "(%zu filtered rows) ", filter_dimensions.row_count - 1);
    display_buffer_subtable(input_data, buff_offset.row, buff_used_rows, buff_offset.col,
                            input_dimensions.col_count + rownum_col_offset > ZTV_MAX_COLS ? ZTV_MAX_COLS :
                            input_dimensions.col_count + rownum_col_offset,
                            cursor_row, cursor_col, header_span, &display_dims);
  }

  endwin();
  free(help_suffix);
  free(row_filter);
  free(find);
  return 0;
}

void display_cell(char *str, int row, int col) {
  size_t len = strlen(str);
  if (has_multibyte_char(str, ZTV_CELL_DISPLAY_WIDTH) == 0) {
    int ch = str[ZTV_CELL_DISPLAY_WIDTH - 1];
    str[ZTV_CELL_DISPLAY_WIDTH - 1] = '\0'; // ensure a blank space between cells
    mvprintw(row, col * ZTV_CELL_DISPLAY_WIDTH, "%-*s", ZTV_CELL_DISPLAY_WIDTH, str);
    str[ZTV_CELL_DISPLAY_WIDTH - 1] = ch;
  } else {
    size_t used_width;
    int err = 0;
    unsigned char *s = (unsigned char *)str;
    size_t nbytes =
      utf8_bytes_up_to_max_width_and_replace_newlines(s, len, ZTV_CELL_DISPLAY_WIDTH - 2, &used_width, &err);
    // extract the substring up to nbytes
    char *substring = malloc(nbytes + 1);
    if (!substring) {
      // Handle memory allocation error
      fprintf(stderr, "Out of memory!\n");
      return;
    }
    memcpy(substring, str, nbytes);
    substring[nbytes] = '\0';

    // convert the substring to wide characters
    wchar_t wsubstring[256]; // Ensure this buffer is large enough
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    const char *p = substring;
    size_t wlen = mbsrtowcs(wsubstring, &p, sizeof(wsubstring) / sizeof(wchar_t), &state);
    if (wlen == (size_t)-1) {
      fprintf(stderr, "Unable to convert to wide chars: %s\n", str);
      free(substring);
      return;
    }

    // move to the desired position
    move(row, col * ZTV_CELL_DISPLAY_WIDTH);

    // print the wide-character string with right padding
    addnwstr(wsubstring, wlen);
    for (size_t k = used_width; k < ZTV_CELL_DISPLAY_WIDTH; k++)
      addch(' ');

    free(substring);
  }
}

void display_buffer_subtable(char buffer[ZTV_BUFFER_ROWS][ZTV_MAX_COLS][ZTV_MAX_CELL_LEN], size_t start_row,
                             size_t buffer_used_row_count, size_t start_col, size_t max_col_count, size_t cursor_row,
                             size_t cursor_col, size_t input_header_span, struct display_dims *ddims) {
  erase(); // use erase() instead of clear() to avoid changes being saved to
           // screen buffer history
  const size_t DATA_START_ROW = input_header_span;
  const size_t SCREEN_DATA_START_ROW = ddims->header_span;

  size_t SCREEN_DATA_ROWS = ddims->rows > SCREEN_DATA_START_ROW + 1 ? ddims->rows - SCREEN_DATA_START_ROW - 1 : 2;

  size_t end_row = start_row + SCREEN_DATA_ROWS + DATA_START_ROW;
  if (end_row > buffer_used_row_count)
    end_row = buffer_used_row_count;

  size_t end_col = start_col + ddims->columns / ZTV_CELL_DISPLAY_WIDTH;
  if (end_col > max_col_count)
    end_col = max_col_count;

  const char *cursor_value = NULL;
  // First, display header row (buffer[0]) at screen row 0
  attron(A_REVERSE);
  for (size_t j = start_col; j < end_col; j++) {
    if (cursor_row == 0 && cursor_col + start_col == j) {
      attroff(A_REVERSE);
      display_cell(buffer[0][j], 0, j - start_col);
      attron(A_REVERSE);
      cursor_value = buffer[0][j];
    } else
      display_cell(buffer[0][j], 0, j - start_col);
  }
  attroff(A_REVERSE);

  // Then display data starting from start_row + DATA_START_ROW
  size_t screen_row = SCREEN_DATA_START_ROW;
  for (size_t i = start_row + DATA_START_ROW; i < end_row; i++, screen_row++) {
    for (size_t j = start_col; j < end_col; j++) {
      if (screen_row == cursor_row && j == cursor_col + start_col) {
        attron(A_REVERSE);
        display_cell(buffer[i][j], screen_row, j - start_col);
        attroff(A_REVERSE);
        cursor_value = buffer[i][j];
      } else {
        display_cell(buffer[i][j], screen_row, j - start_col);
      }
    }
  }

  if (cursor_value) {
    if (!*ztv_status_text)
      sprintf(ztv_status_text, "? for help ");
    ztv_set_status(ddims, ztv_status_text);
    mvprintw(ddims->rows - ddims->footer_span, strlen(ztv_status_text), "%s", cursor_value);
  } else {
    if (!*ztv_status_text)
      sprintf(ztv_status_text, "? for help ");
    ztv_set_status(ddims, ztv_status_text);
  }
  refresh();
}
