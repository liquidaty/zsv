#ifndef ZSVSHEET_PATTERN_H
#define ZSVSHEET_PATTERN_H

#include <stddef.h>
#include <stdint.h>
#include "../utils/pcre2-8/pcre2-8.h"

/**
 * A search or filter pattern entered by the user.
 *
 * Syntax accepted by zsvsheet_pattern_parse():
 *   abc        literal substring, ignoring case (Unicode simple case mapping)
 *   \/abc      literal substring "/abc" (the escape is recognized only at offset 0)
 *   /ab[Cc]    regex, unterminated; case-sensitive, so also how a literal is matched in exact case
 *   /ab[Cc]/   regex, terminated
 *   /abc/i     regex, ignoring case; `i` is the only flag
 *   /a\/i      regex "a\/i" -- the escaped slash is not a terminator, so this is
 *              how a pattern ending in "/<flag letter>" is written
 * An empty regex would match every cell, so it is rejected rather than accepted.
 *
 * The scan for the terminating slash is byte-wise, which is safe for UTF-8: '/'
 * (0x2F) and '\' (0x5C) are ASCII and cannot occur as a continuation byte.
 */
struct zsvsheet_pattern {
  const char *literal; // NULL iff this is a regex; may point into `owned`
  size_t literal_len;
  char *owned;                 // NULL when `literal` is borrowed from the caller
  regex_handle_t *regex;       // NULL iff this is a literal
  int32_t *fold;               // zsv_strfold() of `literal` when a caseless scan is needed, else NULL
  size_t fold_len;             // code points in `fold`; 0 iff fold == NULL
  unsigned char exact : 1;     // whole-cell match (literal only)
  unsigned char caseless : 1;  // regex compiled with /i (a literal always ignores case)
  unsigned char ascii : 1;     // `fold` (when set) is pure ASCII: the byte-scanning kernel applies
  unsigned char malformed : 1; // `fold` has a byte that is not UTF-8: skip the row-span pre-filter
  unsigned char _ : 4;
};

enum zsvsheet_pattern_status {
  zsvsheet_pattern_status_ok = 0,
  zsvsheet_pattern_status_memory,
  zsvsheet_pattern_status_empty,      // regex body was empty
  zsvsheet_pattern_status_syntax,     // regex did not compile
  zsvsheet_pattern_status_unsupported // regex syntax used in a build without pcre2
};

/**
 * Parse user input. On every exit *p is left consistent and safe to free.
 * @param errbuf if non-NULL, receives the regex compiler's diagnostic on a
 *               syntax error (always null-terminated)
 */
enum zsvsheet_pattern_status zsvsheet_pattern_parse(struct zsvsheet_pattern *p, const char *spec, char *errbuf,
                                                    size_t errbuflen);

/**
 * Build a case-sensitive literal pattern that borrows `s`, which must outlive *p.
 * No allocation, so this cannot fail.
 * @param exact if non-zero, match the whole cell rather than a substring
 */
void zsvsheet_pattern_literal(struct zsvsheet_pattern *p, const char *s, char exact);

/**
 * @return non-zero if s[0, len) matches
 */
int zsvsheet_pattern_match(const struct zsvsheet_pattern *p, const unsigned char *s, size_t len);

/**
 * Cheap pre-filter over a whole row's byte span, for callers that would otherwise
 * match every cell in turn. A literal absent from the span is absent from every
 * cell in it; a regex has no equivalent test, so this conservatively returns 1.
 *
 * @return 0 only if no cell within s[0, len) can possibly match
 */
int zsvsheet_pattern_span_might_match(const struct zsvsheet_pattern *p, const unsigned char *s, size_t len);

/**
 * @return non-zero if *p holds a pattern (as opposed to being zero-initialized)
 */
int zsvsheet_pattern_is_set(const struct zsvsheet_pattern *p);

/**
 * Release *p and re-zero it, so calling this twice is safe
 */
void zsvsheet_pattern_free(struct zsvsheet_pattern *p);

/**
 * @return the status-bar message for a search that matched nothing; for a
 *         case-sensitive regex it also says how to ignore case
 */
const char *zsvsheet_pattern_not_found_text(const struct zsvsheet_pattern *p);

/**
 * @return a status-bar message for a non-ok status
 */
const char *zsvsheet_pattern_status_text(enum zsvsheet_pattern_status status);

/**
 * The message to show for a failed parse: the regex compiler's own diagnostic when
 * it produced one, else a generic description of the status.
 * @param errbuf the buffer passed to zsvsheet_pattern_parse(); may be NULL
 */
const char *zsvsheet_pattern_status_message(enum zsvsheet_pattern_status status, const char *errbuf);

#endif
