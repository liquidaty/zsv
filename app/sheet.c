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
#ifdef HAVE_NCURSESW
#include <ncursesw/ncurses.h>
#else
#include <ncurses/ncurses.h>
#endif // HAVE_NCURSESW
#else
#if __has_include(<curses.h>)
#include <curses.h>
#elif __has_include(<ncursesw/curses.h>)
#include <ncursesw/curses.h>
#else
#error Cannot find ncurses include file!
#endif
#endif

#include <locale.h>
#include <wchar.h>
#include <pthread.h>

#define ZSV_COMMAND sheet
#include "zsv_command.h"

#include "../include/zsv/ext/sheet.h"
#include "sheet/sheet_internal.h"
#include "sheet/screen_buffer.c"
#include "sheet/procedure.c"

/* TODO: move this somewhere else like common or utils */
#define UNUSED(X) ((void)X)

struct zsvsheet_opts {
  char hide_row_nums; // if 1, will load row nums
  const char *find;
  size_t found_rownum;
};

#include "sheet/utf8-width.c"
#include "sheet/ui_buffer.c"
#include "sheet/index.c"
#include "sheet/read-data.c"
#include "sheet/key-bindings.c"

#define ZSVSHEET_CELL_DISPLAY_MIN_WIDTH 10
static size_t zsvsheet_cell_display_width(struct zsvsheet_ui_buffer *ui_buffer,
                                          struct zsvsheet_display_dimensions *ddims) {
  size_t width = ddims->columns / (ui_buffer->dimensions.col_count + (ui_buffer->rownum_col_offset ? 1 : 0));
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

struct zsvsheet_builtin_proc_state {
  struct zsvsheet_display_info display_info;
  char *find;
  struct zsv_prop_handler *custom_prop_handler;
  const char *opts_used;
};

static void get_subcommand(const char *prompt, char *buff, size_t buffsize, int footer_row) {
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

zsvsheet_status zsvsheet_ext_prompt(struct zsvsheet_proc_context *ctx, char *buffer, size_t bufsz, const char *fmt,
                                    ...) {
  struct zsvsheet_builtin_proc_state *state = (struct zsvsheet_builtin_proc_state *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;

  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  char prompt_buffer[256] = {0};

  va_list argv;
  va_start(argv, fmt);
  int n = vsnprintf(prompt_buffer, sizeof(prompt_buffer), fmt, argv);
  va_end(argv);

  if (!(n > 0 && (size_t)n < sizeof(prompt_buffer)))
    return zsvsheet_status_ok;

  get_subcommand(prompt_buffer, buffer, bufsz, prompt_footer_row);
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

struct zsvsheet_key_data *zsvsheet_key_handlers = NULL;
struct zsvsheet_key_data **zsvsheet_next_key_handler = &zsvsheet_key_handlers;

/* Common page movement function */
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

/* Common horizontal movement between extremes */
static zsvsheet_status zsvsheet_move_hor_end(struct zsvsheet_display_info *di, bool right) {
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);

  if (right) {
    // to do: directly set current_ui_buffer->cursor_col and buff_offset.col
    while (cursor_right(di->dimensions->columns, zsvsheet_cell_display_width(current_ui_buffer, di->dimensions),
                        current_ui_buffer->dimensions.col_count + current_ui_buffer->rownum_col_offset >
                            zsvsheet_screen_buffer_cols(current_ui_buffer->buffer)
                          ? zsvsheet_screen_buffer_cols(current_ui_buffer->buffer)
                          : current_ui_buffer->dimensions.col_count + current_ui_buffer->rownum_col_offset,
                        &current_ui_buffer->cursor_col, &current_ui_buffer->buff_offset.col) > 0)
      ;
    {}
  } else {
    current_ui_buffer->cursor_col = 0;
    current_ui_buffer->buff_offset.col = 0;
  }
  return zsvsheet_status_ok;
}

// zsvsheet_handle_find_next: return non-zero if a result was found
char zsvsheet_handle_find_next(struct zsvsheet_ui_buffer *uib, const char *needle, struct zsvsheet_opts *zsvsheet_opts,
                               size_t header_span, struct zsvsheet_display_dimensions *ddims, int *update_buffer,
                               struct zsv_prop_handler *custom_prop_handler, const char *opts_used) {
  if (zsvsheet_find_next(uib, needle, zsvsheet_opts, header_span, custom_prop_handler, opts_used) > 0) {
    *update_buffer = zsvsheet_goto_input_raw_row(uib, zsvsheet_opts->found_rownum, header_span, ddims, (size_t)-1);
    return 1;
  }
  zsvsheet_priv_set_status(ddims, 1, "Not found");
  return 0;
}

/* Find and find-next handler */
static zsvsheet_status zsvsheet_find(struct zsvsheet_builtin_proc_state *state, bool next) {
  char prompt_buffer[256] = {0};
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *current_ui_buffer = *(di->ui_buffers.current);
  struct zsvsheet_opts zsvsheet_opts = {0};
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);

  if (!next) {
    get_subcommand("Find", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row);
    if (*prompt_buffer == '\0') {
      goto no_input;
    } else {
      free(state->find);
      state->find = strdup(prompt_buffer);
    }
  }

  if (state->find) {
    zsvsheet_handle_find_next(current_ui_buffer, state->find, &zsvsheet_opts, di->header_span, di->dimensions,
                              &di->update_buffer, state->custom_prop_handler, state->opts_used);
  }

no_input:
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_open_file_handler(struct zsvsheet_proc_context *ctx) {
  // TODO: should be PATH_MAX but that's going to be about a page and compiler
  //       might complain about stack being too large. Probably move to handler
  //       state or something.
  char prompt_buffer[256] = {0};
  struct zsvsheet_builtin_proc_state *state = (struct zsvsheet_builtin_proc_state *)ctx->subcommand_context;

  struct zsvsheet_display_info *di = &state->display_info;
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  int err;

  UNUSED(ctx);

  get_subcommand("File to open", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row);
  if (*prompt_buffer == '\0')
    goto no_input;

  if ((err = zsvsheet_ui_buffer_open_file(prompt_buffer, NULL, NULL, state->custom_prop_handler, state->opts_used,
                                          di->ui_buffers.base, di->ui_buffers.current))) {
    if (err > 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "%s: %s", prompt_buffer, strerror(err));
    else if (err < 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "Unexpected error");
    else
      zsvsheet_priv_set_status(di->dimensions, 1, "Not found: %s", prompt_buffer);
    return zsvsheet_status_ignore;
  }
no_input:
  return zsvsheet_status_ok;
}

static zsvsheet_status zsvsheet_filter_handler(struct zsvsheet_proc_context *ctx) {
  char prompt_buffer[256] = {0};
  struct zsvsheet_builtin_proc_state *state = (struct zsvsheet_builtin_proc_state *)ctx->subcommand_context;
  struct zsvsheet_display_info *di = &state->display_info;
  struct zsvsheet_ui_buffer *current_ui_buffer = *state->display_info.ui_buffers.current;
  int prompt_footer_row = (int)(di->dimensions->rows - di->dimensions->footer_span);
  int err;

  UNUSED(ctx);

  get_subcommand("Filter", prompt_buffer, sizeof(prompt_buffer), prompt_footer_row);
  if (*prompt_buffer == '\0')
    goto no_input;

  if ((err = zsvsheet_ui_buffer_open_file(current_ui_buffer->filename, &current_ui_buffer->zsv_opts, prompt_buffer,
                                          state->custom_prop_handler, state->opts_used, di->ui_buffers.base,
                                          di->ui_buffers.current))) {
    if (err > 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "%s: %s", current_ui_buffer->filename, strerror(err));
    else if (err < 0)
      zsvsheet_priv_set_status(di->dimensions, 1, "Unexpected error");
    else
      zsvsheet_priv_set_status(di->dimensions, 1, "Not found: %s", prompt_buffer);
    return zsvsheet_status_ignore;
  }
  if (current_ui_buffer->dimensions.row_count < 2) {
    zsvsheet_ui_buffer_pop(di->ui_buffers.base, di->ui_buffers.current, NULL);
    zsvsheet_priv_set_status(di->dimensions, 1, "Not found: %s", prompt_buffer);
  }
no_input:
  return zsvsheet_status_ok;
}

/* We do most procedures in one handler. More complex procedures can be
 * separated into their own handlers.
 */
zsvsheet_status zsvsheet_builtin_proc_handler(struct zsvsheet_proc_context *ctx) {
  struct zsvsheet_builtin_proc_state *state = (struct zsvsheet_builtin_proc_state *)ctx->subcommand_context;
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
  }

  return zsvsheet_status_error;
}

/* clang-format off */
struct builtin_proc_desc {
  int proc_id;
  const char *name;
  zsvsheet_proc_fn handler;
} builtin_procedures[] = {
  { zsvsheet_builtin_proc_quit,             "quit", zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_escape,           NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_bottom,      NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_top,         NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_first_col,   NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_pg_down,          "pageup", zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_pg_up,            "pagedn", zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_last_col,    NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_up,          NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_down,        NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_left,        NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_move_right,       NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_find,             NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_find_next,        NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_resize,           NULL,   zsvsheet_builtin_proc_handler },
  { zsvsheet_builtin_proc_open_file,        "open",   zsvsheet_open_file_handler  },
  { zsvsheet_builtin_proc_filter,           "filter", zsvsheet_filter_handler     },
  { -1, NULL, NULL }
};
/* clang-format on */

void zsvsheet_register_builtin_procedures(void) {
  for (struct builtin_proc_desc *desc = builtin_procedures; desc->proc_id != -1; ++desc) {
    if (zsvsheet_register_builtin_proc(desc->proc_id, desc->name, desc->handler) < 0) {
      fprintf(stderr, "failed to register builtin procedure\n");
    }
  }
}

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
  display_buffer_subtable(current_ui_buffer, header_span, &display_dims);

  zsvsheet_register_builtin_procedures();

  /* TODO: allow user to pass key binding choice and dmux here */
  zsvsheet_register_vim_key_bindings();
  // zsvsheet_register_emacs
  //...
  //

  int ch;
  struct zsvsheet_builtin_proc_state handler_state = {
    .display_info.ui_buffers.base = &ui_buffers,
    .display_info.ui_buffers.current = &current_ui_buffer,
    .display_info.dimensions = &display_dims,
    .display_info.header_span = header_span,
    .find = NULL,
    .custom_prop_handler = custom_prop_handler,
    .opts_used = opts_used,
  };

  zsvsheet_status status;

  halfdelay(2); // now ncurses getch() will fire every 2-tenths of a second so we can check for status update

  while (true) {
    char *status_msg = NULL;
    ch = getch();

    handler_state.display_info.update_buffer = false;

    pthread_mutex_lock(&current_ui_buffer->mutex);
    status_msg = current_ui_buffer->status;
    if (current_ui_buffer->index_ready &&
        current_ui_buffer->dimensions.row_count != current_ui_buffer->index->row_count + 1) {
      current_ui_buffer->dimensions.row_count = current_ui_buffer->index->row_count + 1;
      handler_state.display_info.update_buffer = true;
    }
    pthread_mutex_unlock(&current_ui_buffer->mutex);

    zsvsheet_priv_set_status(&display_dims, 1, "");

    if (ch != ERR) {
      status = zsvsheet_key_press(ch, &handler_state);
      if (status == zsvsheet_status_exit)
        break;
      if (status != zsvsheet_status_ok)
        continue;
    }

    if (handler_state.display_info.update_buffer && current_ui_buffer->filename) {
      struct zsvsheet_opts zsvsheet_opts = {0};
      if (read_data(&current_ui_buffer, NULL, current_ui_buffer->input_offset.row, current_ui_buffer->input_offset.col,
                    header_span, &zsvsheet_opts, custom_prop_handler, opts_used)) {
        zsvsheet_priv_set_status(&display_dims, 1, "Unexpected error!"); // to do: better error message
        continue;
      }
    }

    if (status_msg)
      zsvsheet_priv_set_status(&display_dims, 1, status_msg);

    display_buffer_subtable(current_ui_buffer, header_span, &display_dims);
  }

  endwin();
  free(handler_state.find);
  zsvsheet_ui_buffers_delete(ui_buffers);
  zsvsheet_key_handlers_delete(&zsvsheet_key_handlers, &zsvsheet_next_key_handler);
  return 0;
}

const char *display_cell(struct zsvsheet_screen_buffer *buff, size_t data_row, size_t data_col, int row, int col,
                         size_t cell_display_width) {
  char *str = (char *)zsvsheet_screen_buffer_cell_display(buff, data_row, data_col);
  size_t len = str ? strlen(str) : 0;
  if (len == 0 || has_multibyte_char(str, len < cell_display_width ? len : cell_display_width) == 0)
    mvprintw(row, col * cell_display_width, "%-*.*s", cell_display_width, cell_display_width - 1, str);
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
      return str;
    }

    // move to the desired position
    move(row, col * cell_display_width);

    // print the wide-character string with right padding
    addnwstr(wsubstring, wlen);
    for (size_t k = used_width; k < cell_display_width; k++)
      addch(' ');
  }
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

  zsvsheet_priv_set_status(ddims, 0, "? for help ");
  if (cursor_value)
    mvprintw(ddims->rows - ddims->footer_span, strlen(zsvsheet_status_text), "%s", cursor_value);
  refresh();
}
