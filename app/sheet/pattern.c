#include <stdlib.h>
#include <string.h>
#include "pattern.h"

#if defined(WIN32) || defined(_WIN32)
#ifndef NO_MEMMEM
#define NO_MEMMEM
#endif
#include <zsv/utils/memmem.h>
#endif

#ifdef HAVE_PCRE2_8
// Offset of the last '/' in s[0, len) that is not escaped by an odd run of
// backslashes, else len. Byte-wise scanning is UTF-8-safe: '/' and '\' are ASCII
// and never appear as a continuation byte.
static size_t zsvsheet_pattern_last_unescaped_slash(const char *s, size_t len) {
  size_t found = len;
  int escaped = 0;
  for (size_t i = 0; i < len; i++) {
    if (escaped)
      escaped = 0;
    else if (s[i] == '\\')
      escaped = 1;
    else if (s[i] == '/')
      found = i;
  }
  return found;
}
#endif

// Take ownership of a copy of s. On failure literal/literal_len keep the zeroed
// values the caller's memset left, so the pair is never half-set.
static enum zsvsheet_pattern_status zsvsheet_pattern_own_literal(struct zsvsheet_pattern *p, const char *s) {
  size_t len = strlen(s);
  if (!(p->owned = malloc(len + 1)))
    return zsvsheet_pattern_status_memory;
  memcpy(p->owned, s, len + 1);
  p->literal = p->owned;
  p->literal_len = len;
  return zsvsheet_pattern_status_ok;
}

void zsvsheet_pattern_literal(struct zsvsheet_pattern *p, const char *s, char exact) {
  memset(p, 0, sizeof(*p));
  if (!s)
    return;
  p->literal = s;
  p->literal_len = strlen(s);
  p->exact = exact ? 1 : 0;
}

enum zsvsheet_pattern_status zsvsheet_pattern_parse(struct zsvsheet_pattern *p, const char *spec, char *errbuf,
                                                    size_t errbuflen) {
  memset(p, 0, sizeof(*p));
  if (errbuf && errbuflen)
    *errbuf = '\0';
  if (!spec || !*spec)                    // an empty literal can never match, so say so rather than
    return zsvsheet_pattern_status_empty; // return a pattern that silently finds nothing

  if (spec[0] == '\\' && spec[1] == '/') // escaped: a literal starting with '/'
    return zsvsheet_pattern_own_literal(p, spec + 1);
  if (spec[0] != '/')
    return zsvsheet_pattern_own_literal(p, spec);

#ifdef HAVE_PCRE2_8
  const char *body = spec + 1;
  size_t body_len = strlen(body);
  uint32_t flags = 0;
  size_t close = zsvsheet_pattern_last_unescaped_slash(body, body_len);
  if (close < body_len) {
    size_t i = close + 1;
    for (; i < body_len; i++) {
      if (body[i] == 'i')
        flags |= ZSV_PCRE2_8_CASELESS;
      else
        break;
    }
    if (i == body_len)
      body_len = close; // every trailing byte was a flag, so this slash terminates
    else
      flags = 0; // not a terminator: the whole remainder is the pattern
  }
  if (body_len == 0) // an empty regex matches every cell; that is never what was meant
    return zsvsheet_pattern_status_empty;

  char *re = malloc(body_len + 1);
  if (!re)
    return zsvsheet_pattern_status_memory;
  memcpy(re, body, body_len);
  re[body_len] = '\0';
  p->regex = zsv_pcre2_8_new_ex(re, flags, errbuf, errbuflen);
  free(re);
  return p->regex ? zsvsheet_pattern_status_ok : zsvsheet_pattern_status_syntax;
#else
  return zsvsheet_pattern_status_unsupported;
#endif
}

int zsvsheet_pattern_match(const struct zsvsheet_pattern *p, const unsigned char *s, size_t len) {
  if (!s)
    len = 0;
#ifdef HAVE_PCRE2_8
  if (p->regex)
    return zsv_pcre2_8_match(p->regex, s ? s : (const unsigned char *)"", len);
#endif
  if (!p->literal_len || !len)
    return 0;
  if (p->exact)
    return len == p->literal_len && !memcmp(s, p->literal, len);
  return memmem(s, len, p->literal, p->literal_len) != NULL;
}

int zsvsheet_pattern_span_might_match(const struct zsvsheet_pattern *p, const unsigned char *s, size_t len) {
  if (!p->literal) // only a literal can be ruled out without matching cell by cell
    return 1;
  if (!p->literal_len || !s || !len)
    return 0;
  return memmem(s, len, p->literal, p->literal_len) != NULL;
}

int zsvsheet_pattern_is_set(const struct zsvsheet_pattern *p) {
  return p->literal != NULL || p->regex != NULL;
}

void zsvsheet_pattern_free(struct zsvsheet_pattern *p) {
#ifdef HAVE_PCRE2_8
  zsv_pcre2_8_delete(p->regex); // NULL-safe
#endif
  free(p->owned);
  memset(p, 0, sizeof(*p));
}

const char *zsvsheet_pattern_status_text(enum zsvsheet_pattern_status status) {
  switch (status) {
  case zsvsheet_pattern_status_ok:
    return "";
  case zsvsheet_pattern_status_memory:
    return "Out of memory";
  case zsvsheet_pattern_status_empty:
    return "Empty search pattern";
  case zsvsheet_pattern_status_syntax:
    return "Invalid regular expression";
  case zsvsheet_pattern_status_unsupported:
    return "Regular expressions are not available in this build";
  }
  return "Invalid search pattern";
}

const char *zsvsheet_pattern_status_message(enum zsvsheet_pattern_status status, const char *errbuf) {
  return errbuf && *errbuf ? errbuf : zsvsheet_pattern_status_text(status);
}
