/*
 * Unit test of the sheet whole-cell overlay (app/sheet/cell-overlay.c): when the overlay appears,
 * and how it word-wraps a cell value into lines measured in display columns rather than bytes
 */
#include <stdio.h>
#include <string.h>
#include "../sheet/cell-overlay.h"
#include "../sheet/sheet_internal.h" // ZSVSHEET_INPUT_TIMEOUT_TENTHS, the tick the delay is measured in

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

#define TICK_MS ((size_t)ZSVSHEET_INPUT_TIMEOUT_TENTHS * 100)

struct expected_line {
  const char *text;
  size_t cols;
};

#define MAX_LINES 16

static int check_wrap(const char *value, size_t max_cols, size_t max_lines, const struct expected_line *expected,
                      size_t expected_count, size_t expected_more) {
  struct zsvsheet_cell_overlay_line lines[MAX_LINES];
  size_t more = (size_t)-1;
  size_t n = expected_count > MAX_LINES ? MAX_LINES : expected_count;
  CHECK(zsvsheet_cell_overlay_wrap((const unsigned char *)value, strlen(value), max_cols, lines, max_lines, &more) ==
        expected_count);
  CHECK(more == expected_more);
  for (size_t i = 0; i < n; i++) {
    CHECK(lines[i].len == strlen(expected[i].text));
    CHECK(!memcmp(lines[i].text, expected[i].text, lines[i].len));
    CHECK(lines[i].cols == expected[i].cols);
  }
  return 0;
}

// The overlay's decision: the same numbers a 80-column screen gives with "? for help " in front
static int check_wanted(char enabled, unsigned delay_ms, size_t idle_ticks, size_t footer_col, size_t value_cols,
                        size_t columns, int expected) {
  struct zsvsheet_cell_overlay_settings settings = {.enabled = enabled, .delay_ms = delay_ms};
  int wanted = zsvsheet_cell_overlay_wanted(&settings, idle_ticks, footer_col, value_cols, columns);
  CHECK(wanted == expected);
  return 0;
}

// The sanitizer makes a value safe to draw and to measure: what it writes is len bytes, with
// everything a terminal would draw at a width of its own replaced by spaces
static int check_sanitize(const char *value, const char *expected) {
  size_t len = strlen(value);
  unsigned char out[MAX_LINES * 4 + 1];
  CHECK(len == strlen(expected) && len < sizeof(out));
  memset(out, 0xff, sizeof(out));
  zsvsheet_cell_overlay_sanitize(out, (const unsigned char *)value, len);
  CHECK(!memcmp(out, expected, len));
  CHECK(out[len] == 0xff); // writes len bytes and no more
  return 0;
}

static int check_width(const char *value, size_t expected) {
  CHECK(zsvsheet_cell_overlay_text_width((const unsigned char *)value, strlen(value)) == expected);
  return 0;
}

// What the overlay draws is what it measured: sanitizing (one space per byte for anything a
// terminal would draw blank, which is what the overlay draws) leaves the width unchanged
static int check_sanitize_keeps_width(const char *value) {
  size_t len = strlen(value);
  unsigned char out[MAX_LINES * 4 + 1];
  CHECK(len < sizeof(out));
  zsvsheet_cell_overlay_sanitize(out, (const unsigned char *)value, len);
  CHECK(zsvsheet_cell_overlay_text_width(out, len) ==
        zsvsheet_cell_overlay_text_width((const unsigned char *)value, len));
  return 0;
}

int main(void) {
  size_t ticks_1s = (1000 + TICK_MS - 1) / TICK_MS;

  // the shipped settings: the overlay is on, after a second of rest
  const struct zsvsheet_cell_overlay_settings *shipped = zsvsheet_cell_overlay_settings();
  CHECK(shipped->enabled);
  CHECK(shipped->delay_ms == 1000);
  CHECK(!check_wanted(shipped->enabled, shipped->delay_ms, ticks_1s - 1, 11, 10, 20, 0));
  CHECK(!check_wanted(shipped->enabled, shipped->delay_ms, ticks_1s, 11, 10, 20, 1));

  // a value that fits on one line is one line
  CHECK(!check_wrap("hello", 10, MAX_LINES, &(struct expected_line){"hello", 5}, 1, 0));

  // lines end at the space that no longer fits
  CHECK(!check_wrap("aaa bbb ccc", 5, MAX_LINES, (struct expected_line[]){{"aaa", 3}, {"bbb", 3}, {"ccc", 3}}, 3, 0));

  // a space the line has no room for ends the line too, so a word that ends exactly at the width
  // is not pushed to the next line
  CHECK(!check_wrap("aa bb cc", 5, MAX_LINES, (struct expected_line[]){{"aa bb", 5}, {"cc", 2}}, 2, 0));

  // a word longer than the line is split at the width, and the walk always advances
  CHECK(!check_wrap("abcdefgh", 3, MAX_LINES, (struct expected_line[]){{"abc", 3}, {"def", 3}, {"gh", 2}}, 3, 0));
  CHECK(!check_wrap("abc defg", 3, MAX_LINES, (struct expected_line[]){{"abc", 3}, {"def", 3}, {"g", 1}}, 3, 0));

  // the run of spaces a line broke at is not part of either line
  CHECK(!check_wrap("aaa   bbb", 5, MAX_LINES, (struct expected_line[]){{"aaa", 3}, {"bbb", 3}}, 2, 0));
  CHECK(!check_wrap("abc  def", 3, MAX_LINES, (struct expected_line[]){{"abc", 3}, {"def", 3}}, 2, 0));

  // a line never breaks at a space it starts with: that space is content the width has room for
  CHECK(!check_wrap(" ab", 2, MAX_LINES, (struct expected_line[]){{" a", 2}, {"b", 1}}, 2, 0));

  // a tab separates words and takes one of the line's columns
  CHECK(!check_wrap("a\tb", 1, MAX_LINES, (struct expected_line[]){{"a", 1}, {"b", 1}}, 2, 0));

  // a line break in the value ends the line; CRLF is one line break
  CHECK(!check_wrap("ab\ncd", 10, MAX_LINES, (struct expected_line[]){{"ab", 2}, {"cd", 2}}, 2, 0));
  CHECK(!check_wrap("ab\r\ncd", 10, MAX_LINES, (struct expected_line[]){{"ab", 2}, {"cd", 2}}, 2, 0));

  // a blank line in the value stays a line
  CHECK(!check_wrap("a\n\nb", 10, MAX_LINES, (struct expected_line[]){{"a", 1}, {"", 0}, {"b", 1}}, 3, 0));

  // spaces after a line break are indentation, not the tail of a break
  CHECK(!check_wrap("ab\n  cd", 10, MAX_LINES, (struct expected_line[]){{"ab", 2}, {"  cd", 4}}, 2, 0));

  // trailing spaces are not part of a line
  CHECK(!check_wrap("abc   ", 10, MAX_LINES, &(struct expected_line){"abc", 3}, 1, 0));

  // widths are counted in columns, not bytes, and a character is never split
  CHECK(!check_wrap("h\xc3\xa9llo", 3, MAX_LINES, (struct expected_line[]){{"h\xc3\xa9l", 3}, {"lo", 2}}, 2, 0));

  // a combining character takes no column, so a line can be longer in bytes than in columns
  CHECK(!check_wrap("e\xcc\x81"
                    "f",
                    1, MAX_LINES, (struct expected_line[]){{"e\xcc\x81", 1}, {"f", 1}}, 2, 0));

  // wide characters take two columns
  CHECK(!check_wrap("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 5, MAX_LINES,
                    (struct expected_line[]){{"\xe6\x97\xa5\xe6\x9c\xac", 4}, {"\xe8\xaa\x9e", 2}}, 2, 0));

  // a character wider than the line is taken anyway, so a line always advances
  CHECK(!check_wrap("\xe6\x97\xa5", 1, MAX_LINES, &(struct expected_line){"\xe6\x97\xa5", 2}, 1, 0));

  // a control character counts one column and an invalid byte one byte, without looping
  CHECK(!check_wrap("ab\x01"
                    "cd",
                    10, MAX_LINES,
                    &(struct expected_line){"ab\x01"
                                            "cd",
                                            5},
                    1, 0));
  CHECK(!check_wrap("ab\x80"
                    "cd",
                    10, MAX_LINES,
                    &(struct expected_line){"ab\x80"
                                            "cd",
                                            5},
                    1, 0));
  CHECK(!check_wrap("\x80"
                    "ab",
                    2, MAX_LINES,
                    (struct expected_line[]){{"\x80"
                                              "a",
                                              2},
                                             {"b", 1}},
                    2, 0));

  // beyond max_lines the remaining lines are counted, not written
  CHECK(!check_wrap("x\nx\nx\nx\nx\nx\nx\nx\nx\nx", 5, 2, (struct expected_line[]){{"x", 1}, {"x", 1}}, 2, 8));
  CHECK(!check_wrap("x\nx\nx", 5, 3, (struct expected_line[]){{"x", 1}, {"x", 1}, {"x", 1}}, 3, 0));

  // an empty value has no lines
  CHECK(!check_wrap("", 10, MAX_LINES, NULL, 0, 0));

  // a value the footer line shows in full: no overlay
  CHECK(!check_wanted(1, 1000, 100, 11, 10, 80, 0));
  CHECK(!check_wanted(1, 1000, 100, 11, 10, 21, 0)); // ends exactly at the screen edge

  // one column past the screen edge: the overlay appears, but only after the delay
  CHECK(!check_wanted(1, 1000, 100, 11, 10, 20, 1));
  CHECK(!check_wanted(1, 1000, ticks_1s - 1, 11, 10, 20, 0));
  CHECK(!check_wanted(1, 1000, ticks_1s, 11, 10, 20, 1));

  // the delay rounds up to whole input timeouts
  CHECK(!check_wanted(1, 1000 + 1, (1000 + 1 + TICK_MS - 1) / TICK_MS - 1, 11, 10, 20, 0));
  CHECK(!check_wanted(1, 1000 + 1, (1000 + 1 + TICK_MS - 1) / TICK_MS, 11, 10, 20, 1));

  // disabled: never
  CHECK(!check_wanted(0, 0, 100000, 11, 10, 20, 0));

  // a value that fits decides it, whatever the footer shows after it (the compare view's partner)
  CHECK(!check_wanted(1, 1000, 100, 11, 5, 80, 0));

  // the sanitizer replaces what a terminal would draw at a width of its own, keeping byte counts
  CHECK(!check_sanitize("ab\tcd", "ab cd"));
  CHECK(!check_sanitize("ab\x01"
                        "cd",
                        "ab cd"));
  CHECK(!check_sanitize("ab\x7f"
                        "cd",
                        "ab cd"));
  CHECK(!check_sanitize("ab\x80"
                        "cd",
                        "ab cd"));
  CHECK(!check_sanitize("ab\ncd", "ab\ncd")); // the wrap reads the line breaks
  CHECK(!check_sanitize("a\xc3\xa9"
                        "b",
                        "a\xc3\xa9"
                        "b"));
  CHECK(!check_sanitize("a\xc2\x85"
                        "b",
                        "a  b")); // a 2-byte control keeps its 2 bytes

  // widths are columns, counted as the footer line shows them
  CHECK(!check_width("abc", 3));
  CHECK(!check_width("a\tb", 3)); // a control or line break: one column
  CHECK(!check_width("a\nb", 3));
  CHECK(!check_width("a\xc3\xa9"
                     "b",
                     3));                             // é: two bytes, one column
  CHECK(!check_width("e\xcc\x81", 1));                // a combining mark: no column
  CHECK(!check_width("\xe6\x97\xa5\xe8\xaa\x9e", 4)); // two wide characters

  // what is drawn as a space per byte is counted by the byte: drawn as itself, a blank or ^X glyph
  // would take columns neither the footer nor the box accounted for
  CHECK(!check_width("a\xc2\x85"
                     "b",
                     4)); // U+0085: two bytes, two columns
  CHECK(!check_width("a\xe2\x80\x8b"
                     "b",
                     5)); // U+200B: three bytes, three columns
  CHECK(!check_width("ab\x80"
                     "cd",
                     5)); // an invalid byte: one column

  // and sanitizing, which is what the overlay draws, never changes the width
  CHECK(!check_sanitize_keeps_width("abc"));
  CHECK(!check_sanitize_keeps_width("a\tb"));
  CHECK(!check_sanitize_keeps_width("a\x01"
                                    "b"));
  CHECK(!check_sanitize_keeps_width("a\nb"));
  CHECK(!check_sanitize_keeps_width("ab\x80"
                                    "cd"));
  CHECK(!check_sanitize_keeps_width("a\xc2\x85"
                                    "b"));
  CHECK(!check_sanitize_keeps_width("a\xe2\x80\x8b"
                                    "b"));
  CHECK(!check_sanitize_keeps_width("a\xc3\xa9"
                                    "b"));
  CHECK(!check_sanitize_keeps_width("e\xcc\x81"));
  CHECK(!check_sanitize_keeps_width("\xe6\x97\xa5\xe8\xaa\x9e"));

  return 0;
}
