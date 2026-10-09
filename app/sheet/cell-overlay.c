// Whole-cell overlay: see cell-overlay.h

#include "cell-overlay.h"
#include "sheet_internal.h" // struct zsvsheet_display_dimensions, ZSVSHEET_INPUT_TIMEOUT_TENTHS
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

static const struct zsvsheet_cell_overlay_settings zsvsheet_cell_overlay_settings_current = {
  .enabled = 1,
  .delay_ms = 1000,
};

const struct zsvsheet_cell_overlay_settings *zsvsheet_cell_overlay_settings(void) {
  return &zsvsheet_cell_overlay_settings_current;
}

// Set while a box is on the screen and nothing has repainted over it
static char cell_overlay_painted;

int zsvsheet_cell_overlay_painted(void) {
  return cell_overlay_painted;
}

// Blank columns between the text and the box edge, and between the box and the screen edge
#define ZSVSHEET_CELL_OVERLAY_H_PAD 1
#define ZSVSHEET_CELL_OVERLAY_H_MARGIN 2
// Wrap width cap: a readable measure on a wide terminal
#define ZSVSHEET_CELL_OVERLAY_MAX_TEXT_COLS 72
// Fewer rows than this above the status bar: no room for the overlay
#define ZSVSHEET_CELL_OVERLAY_MIN_ROWS 2

int zsvsheet_cell_overlay_wanted(const struct zsvsheet_cell_overlay_settings *settings, size_t idle_ticks,
                                 size_t footer_col, size_t value_cols, size_t columns) {
  size_t tick_ms, delay_ticks;

  if (!settings->enabled)
    return 0;
  if (footer_col + value_cols <= columns) // the status bar shows the whole value already
    return 0;
  // idle for the configured time: the main loop counts one tick per input timeout, so the delay
  // rounds up to whole ticks
  tick_ms = (size_t)ZSVSHEET_INPUT_TIMEOUT_TENTHS * 100;
  delay_ticks = settings->delay_ms / tick_ms + (settings->delay_ms % tick_ms != 0);
  return idle_ticks >= delay_ticks;
}

// Whether a terminal draws a character as blank of its own: a control character (ncurses draws
// ^X or a blank), a format, line- or paragraph-separator character, or anything utf8proc gives
// no width for. sanitize() draws each as one space per byte, so each takes its byte count in
// columns and what is measured equals what is drawn
static int cell_overlay_draws_blank(utf8proc_int32_t codepoint) {
  switch (utf8proc_category(codepoint)) {
  case UTF8PROC_CATEGORY_CC:
  case UTF8PROC_CATEGORY_CF:
  case UTF8PROC_CATEGORY_ZL:
  case UTF8PROC_CATEGORY_ZP:
    return 1;
  default:
    return utf8proc_charwidth(codepoint) < 0;
  }
}

// Columns the character at s[i] takes when drawn, and its length in bytes. An invalid byte is
// drawn as a space: one column, one byte
static int cell_overlay_char_at(const unsigned char *s, size_t i, size_t len, size_t *charlen) {
  utf8proc_int32_t codepoint;
  utf8proc_ssize_t n = utf8proc_iterate(s + i, (utf8proc_ssize_t)(len - i), &codepoint);
  if (n <= 0) {
    *charlen = 1;
    return 1;
  }
  *charlen = (size_t)n;
  if (cell_overlay_draws_blank(codepoint))
    return (int)n; // drawn as one space per byte
  return utf8proc_charwidth(codepoint);
}

size_t zsvsheet_cell_overlay_text_width(const unsigned char *s, size_t len) {
  size_t width = 0;
  for (size_t i = 0; i < len;) {
    size_t charlen;
    width += (size_t)cell_overlay_char_at(s, i, len, &charlen);
    i += charlen;
  }
  return width;
}

void zsvsheet_cell_overlay_sanitize(unsigned char *dst, const unsigned char *src, size_t len) {
  for (size_t i = 0; i < len;) {
    utf8proc_int32_t codepoint;
    utf8proc_ssize_t n = utf8proc_iterate(src + i, (utf8proc_ssize_t)(len - i), &codepoint);
    if (n <= 0) {
      dst[i++] = ' '; // an invalid byte
      continue;
    }
    if (cell_overlay_draws_blank(codepoint)) {
      if (codepoint == '\n' || codepoint == '\r')
        memmove(dst + i, src + i, (size_t)n); // the wrap reads the line breaks
      else
        memset(dst + i, ' ', (size_t)n); // one space per byte: what the width above counts
    } else
      memmove(dst + i, src + i, (size_t)n);
    i += (size_t)n;
  }
}

static int cell_overlay_is_space(unsigned char c) {
  return c == ' ' || c == '\t';
}

static int cell_overlay_is_line_break(unsigned char c) {
  return c == '\n' || c == '\r';
}

size_t zsvsheet_cell_overlay_wrap(const unsigned char *value, size_t value_len, size_t max_cols,
                                  struct zsvsheet_cell_overlay_line *lines, size_t max_lines, size_t *more) {
  size_t written = 0, beyond = 0, pos = 0;

  *more = 0;

  while (pos < value_len) {
    size_t scan, width = 0, last_space = (size_t)-1, end, next;

    // where the line ends: a line break, the first character that does not fit, or the end
    scan = pos;
    while (scan < value_len && !cell_overlay_is_line_break(value[scan])) {
      size_t charlen;
      int char_width = cell_overlay_char_at(value, scan, value_len, &charlen);
      if (width + (size_t)char_width > max_cols)
        break;
      if (cell_overlay_is_space(value[scan]))
        last_space = scan;
      width += (size_t)char_width;
      scan += charlen;
    }

    if (scan < value_len && cell_overlay_is_line_break(value[scan])) {
      end = scan;
      next = scan + 1;
      if (next < value_len && cell_overlay_is_line_break(value[next]) && value[next] != value[scan])
        next++; // the two characters of a CRLF pair are one line break
    } else if (scan < value_len && cell_overlay_is_space(value[scan])) {
      end = scan; // the line filled to the width at a space: end it there
      next = scan;
      while (next < value_len && cell_overlay_is_space(value[next]))
        next++; // the whole run the break consumed, not just the one space
    } else if (scan < value_len && last_space != (size_t)-1 && last_space > pos) {
      end = last_space; // end the line before its last space rather than split a word
      next = last_space + 1;
    } else if (scan < value_len) {
      end = scan;       // a word longer than a line: split it
      if (end == pos) { // wider than a whole line: take one character, so the walk advances
        size_t charlen;
        cell_overlay_char_at(value, pos, value_len, &charlen);
        end += charlen;
      }
      next = end;
    } else
      end = next = value_len; // the rest of the value is one line

    while (end > pos && cell_overlay_is_space(value[end - 1]))
      end--; // trailing spaces and tabs are not part of the line

    if (written < max_lines) {
      lines[written].text = value + pos;
      lines[written].len = end - pos;
      lines[written].cols = zsvsheet_cell_overlay_text_width(value + pos, end - pos);
      written++;
    } else
      beyond++;
    pos = next;
  }

  *more = beyond;
  return written;
}

void zsvsheet_cell_overlay_draw(const struct zsvsheet_display_dimensions *ddims, size_t footer_col, const char *value,
                                size_t idle_ticks) {
  const struct zsvsheet_cell_overlay_settings *settings = zsvsheet_cell_overlay_settings();
  size_t value_len = value ? strlen(value) : 0;
  size_t top, avail_rows, text_cols, widest = 0, note_len = 0, written, box_rows, box_col, box_width, box_top, i;
  size_t beyond = 0;
  char note[64];
  unsigned char *text = NULL;
  struct zsvsheet_cell_overlay_line *lines = NULL;
  int cursor_row, cursor_col;

  cell_overlay_painted = 0; // repainting without a box takes the last one off the screen
  if (!value_len)
    return;
  // a value whose byte count fits fits on screen too (a character is at least one byte per
  // column), so the common short value skips the width walk below
  if (footer_col + value_len <= ddims->columns)
    return;
  if (!zsvsheet_cell_overlay_wanted(settings, idle_ticks, footer_col,
                                    zsvsheet_cell_overlay_text_width((const unsigned char *)value, value_len),
                                    ddims->columns))
    return;

  // the box stays below the header row, which is drawn in reverse too: over it, the two would
  // read as one band, with the header's own cells still visible beside the text
  top = ddims->header_span;
  avail_rows = ddims->rows > ddims->footer_span + top ? ddims->rows - ddims->footer_span - top : 0;
  if (avail_rows < ZSVSHEET_CELL_OVERLAY_MIN_ROWS ||
      ddims->columns <= 2 * (ZSVSHEET_CELL_OVERLAY_H_PAD + ZSVSHEET_CELL_OVERLAY_H_MARGIN))
    return;
  text_cols = ddims->columns - 2 * (ZSVSHEET_CELL_OVERLAY_H_PAD + ZSVSHEET_CELL_OVERLAY_H_MARGIN);
  if (text_cols > ZSVSHEET_CELL_OVERLAY_MAX_TEXT_COLS)
    text_cols = ZSVSHEET_CELL_OVERLAY_MAX_TEXT_COLS;

  text = malloc(value_len + 1);
  lines = text ? malloc(avail_rows * sizeof(*lines)) : NULL;
  if (!text || !lines)
    goto out; // no overlay rather than a failed repaint
  zsvsheet_cell_overlay_sanitize(text, (const unsigned char *)value, value_len);
  text[value_len] = '\0';

  written = zsvsheet_cell_overlay_wrap(text, value_len, text_cols, lines, avail_rows, &beyond);
  if (beyond) {
    int n = snprintf(note, sizeof(note), "... %zu more lines", beyond + 1);
    if (n > 0)
      note_len = (size_t)n < sizeof(note) ? (size_t)n : sizeof(note) - 1;
    if (note_len > text_cols) {
      // no room for the count: a mark still says the value goes on. The gates above leave as few
      // as seven screen columns (text_cols 1), where the mark is wider than text_cols but the
      // five-column box it makes still fits on the screen
      note_len = 3;
      memcpy(note, "...", note_len);
    }
    if (note_len)
      written--; // the note row takes the last line's place
  }

  for (i = 0; i < written; i++) {
    if (lines[i].cols > widest)
      widest = lines[i].cols;
  }
  if (note_len > widest)
    widest = note_len;
  if (!widest)
    goto out; // a value of nothing but blanks: no text to show
  // a line is at most text_cols wide, or one character wider where a character does not fit in
  // text_cols at all, and the gates above keep the box that reaches the screen edge out
  box_width = widest + 2 * ZSVSHEET_CELL_OVERLAY_H_PAD;
  box_col = (ddims->columns - box_width) / 2;
  box_rows = written + (note_len ? 1 : 0);
  box_top = top + (avail_rows - box_rows) / 2; // centered between the header and the status bar

  getyx(stdscr, cursor_row, cursor_col); // the box is drawn over the grid; leave the cursor where it was
  // clear the covered rows the whole way across first: grid text drawn in reverse (the cursor's
  // cell) or a cell cut off at a column edge would otherwise show beside the box as part of its band
  for (i = 0; i < box_rows; i++)
    mvprintw((int)(box_top + i), 0, "%*s", (int)ddims->columns, "");
  attron(A_REVERSE);
  for (i = 0; i < box_rows; i++)
    mvprintw((int)(box_top + i), (int)box_col, "%*s", (int)box_width, "");
  for (i = 0; i < written; i++)
    zsvsheet_mvaddnstr_utf8((int)(box_top + i), (int)(box_col + ZSVSHEET_CELL_OVERLAY_H_PAD),
                            (const char *)lines[i].text, lines[i].len);
  if (note_len)
    zsvsheet_mvaddnstr_utf8((int)(box_top + written), (int)(box_col + ZSVSHEET_CELL_OVERLAY_H_PAD), note, note_len);
  attroff(A_REVERSE);
  move(cursor_row, cursor_col);
  cell_overlay_painted = 1; // the grid under it is covered until something repaints

out:
  free(lines);
  free(text);
}
