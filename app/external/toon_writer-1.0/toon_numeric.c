#include "toon_numeric.h"

/* Exact match of the JSON number grammar (RFC 8259), which is also the set of
 * numeric tokens TOON emits/accepts unquoted:
 *   -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
 * Used for unquoted-value classification and for deciding whether a string
 * collides with a number and therefore must be quoted. */
bool is_valid_toon_number(const char *s, size_t n) {
  size_t i = 0;
  if (n == 0)
    return false;
  if (s[i] == '-') {
    if (++i == n)
      return false;
  }
  if (s[i] == '0') {
    i++;
  } else if (s[i] >= '1' && s[i] <= '9') {
    while (i < n && s[i] >= '0' && s[i] <= '9')
      i++;
  } else {
    return false;
  }
  if (i < n && s[i] == '.') {
    i++;
    if (i == n || s[i] < '0' || s[i] > '9')
      return false;
    while (i < n && s[i] >= '0' && s[i] <= '9')
      i++;
  }
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < n && (s[i] == '+' || s[i] == '-'))
      i++;
    if (i == n || s[i] < '0' || s[i] > '9')
      return false;
    while (i < n && s[i] >= '0' && s[i] <= '9')
      i++;
  }
  return i == n;
}
