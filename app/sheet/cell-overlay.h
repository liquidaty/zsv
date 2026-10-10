#ifndef ZSVSHEET_CELL_OVERLAY_H
#define ZSVSHEET_CELL_OVERLAY_H

#include <stddef.h>

struct zsvsheet_display_dimensions;

/* Whole-cell overlay settings: the knobs a future release can expose to the user as saved
 * configuration. zsvsheet_cell_overlay_settings() is their one source, so adding a configuration
 * lookup later touches one function */
struct zsvsheet_cell_overlay_settings {
  char enabled;      // 0 = never show the overlay
  unsigned delay_ms; // input idle time before it appears
};

const struct zsvsheet_cell_overlay_settings *zsvsheet_cell_overlay_settings(void);

// Whether a box is on the screen and nothing has repainted over it: the grid under the box is
// covered until a repaint, so whoever takes a key that would draw elsewhere must repaint first
int zsvsheet_cell_overlay_painted(void);

// Columns a UTF-8 string takes on screen: what a terminal draws for each character, with a
// character drawn blank (see sanitize below) counting its byte count in columns, as sanitize()
// writes it
size_t zsvsheet_cell_overlay_text_width(const unsigned char *s, size_t len);

// Copy src[0..len) to dst (which holds len bytes and may be src), replacing with spaces, one per
// byte, every character a terminal would draw blank: a control character (other than the line
// breaks the wrap reads), a format, line- or paragraph-separator character, and a byte that is not
// valid UTF-8. A replacement keeps its byte count, so what is drawn is exactly what the measure
// above counted. The footer line draws through this mapping too, so the value it shows and the box
// that shows it in full agree
void zsvsheet_cell_overlay_sanitize(unsigned char *dst, const unsigned char *src, size_t len);

// Whether the overlay appears: the feature is enabled, the cursor has rested for the configured
// delay, and the footer line cannot show the whole cell value. value_cols is the width of the
// value's one-line form and footer_col the columns of status text the footer line shows before it
// (what the compare view adds after the value is not part of the cell)
int zsvsheet_cell_overlay_wanted(const struct zsvsheet_cell_overlay_settings *settings, size_t idle_ticks,
                                 size_t footer_col, size_t value_cols, size_t columns);

// One line of a wrapped cell value: a range of the value's bytes (not NUL-terminated) and the
// display columns the line takes on screen
struct zsvsheet_cell_overlay_line {
  const unsigned char *text;
  size_t len;
  size_t cols;
};

// Greedily word-wrap value[0..value_len) to at most max_cols display columns per line. A line
// ends at the space or tab the next word does not fit after, at a space or tab the line itself
// has no room for, at a character that does not fit (a word longer than a line is split at the
// width), or at a line break in the value; no UTF-8 character is split, a character wider than
// max_cols takes its own line, and the run of spaces and tabs a break consumed is part of neither
// line.
// Writes at most max_lines lines to lines[] and returns how many it wrote; *more is set to the
// number of further lines the whole value needs (0 if all of it fit). An empty value has no
// lines. A control character or an invalid byte counts as one column, and the overlay draws both
// as a space, so a line's measured width is the width it takes on screen
size_t zsvsheet_cell_overlay_wrap(const unsigned char *value, size_t value_len, size_t max_cols,
                                  struct zsvsheet_cell_overlay_line *lines, size_t max_lines, size_t *more);

// Draw the whole-cell overlay over the screen just drawn, when zsvsheet_cell_overlay_wanted()
// says so: value is the cell's value, which the overlay wraps, centers and shows, and footer_col
// is the columns of status text the footer line shows before it. idle_ticks is how many input
// timeouts have passed since the last key. Call it after every repaint, with a NULL value when
// the cursor is on no cell: a call that draws nothing clears zsvsheet_cell_overlay_painted()
void zsvsheet_cell_overlay_draw(const struct zsvsheet_display_dimensions *ddims, size_t footer_col, const char *value,
                                size_t idle_ticks);

#endif
