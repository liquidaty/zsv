#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "pcre2-8.h"

// Latch so a per-cell match error is reported once per process, not once per cell.
// Both callers can match from more than one thread (sheet's filter worker, and
// select's parallel workers), so this is formally a data race. It is left unsynchronized
// deliberately: the only observable effect is a duplicated stderr line, and an aligned
// int cannot tear on any supported target. Made atomic only if that stops being true.
static int zsv_pcre2_8_match_error_reported = 0;

/**
 * @brief Defines the internal structure of the handle.
 * We only need to store the compiled code.
 */
struct zsv_pcre2_handle {
  pcre2_code *re;
  pcre2_match_data *cached_match_data;
};

/**
 * @brief Report a failure to errbuf if given, else to stderr.
 */
static void zsv_pcre2_8_report(char *errbuf, size_t errbuflen, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (errbuf && errbuflen)
    vsnprintf(errbuf, errbuflen, fmt, args); // always null-terminates
  else {
    // stderr, not stdout: for `zsv select` stdout is the CSV output stream
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
  }
  va_end(args);
}

/**
 * @brief Implementation of zsv_pcre2_8_new_ex.
 */
regex_handle_t *zsv_pcre2_8_new_ex(const char *pattern, uint32_t flags, char *errbuf, size_t errbuflen) {
  if (errbuf && errbuflen)
    *errbuf = '\0';

  regex_handle_t *handle = calloc(1, sizeof(regex_handle_t));
  if (handle == NULL) {
    zsv_pcre2_8_report(errbuf, errbuflen, "Out of memory compiling regular expression");
    return NULL;
  }

  int error_number;
  PCRE2_SIZE error_offset;

  // utf-8 and multiline support
  uint32_t compile_options = PCRE2_UTF | PCRE2_MULTILINE;
  if (flags & ZSV_PCRE2_8_CASELESS)
    compile_options |= PCRE2_CASELESS;

  pcre2_compile_context *compile_context = pcre2_compile_context_create_8(NULL);
  if (compile_context == NULL) {
    zsv_pcre2_8_report(errbuf, errbuflen, "Error: Failed to create compile context.");
    free(handle);
    return NULL;
  }

  // set newline convention to '\0'
  pcre2_set_newline_8(compile_context, PCRE2_NEWLINE_NUL);

  handle->re = pcre2_compile_8((PCRE2_SPTR)pattern,   // the pattern
                               PCRE2_ZERO_TERMINATED, // pattern is zero-terminated
                               compile_options,       // caller flags + UTF + MULTILINE
                               &error_number,         // for error number
                               &error_offset,         // for error offset
                               compile_context        // use our configured compile context
  );

  // free the compile context (no longer needed)
  pcre2_compile_context_free_8(compile_context);

  if (handle->re == NULL) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message_8(error_number, buffer, sizeof(buffer));
    // diagnostic first and the pattern last and bounded: errbuf is 256 bytes and a
    // pattern can be longer than that, which would truncate away the actual reason
    zsv_pcre2_8_report(errbuf, errbuflen, "Invalid regular expression at offset %zu: %s (in \"%.60s\")",
                       (size_t)error_offset, buffer, pattern ? pattern : "");
    free(handle);
    return NULL;
  }

  // pcre2_match() requires match data; without it every match would fail
  handle->cached_match_data = pcre2_match_data_create_from_pattern_8(handle->re, NULL);
  if (handle->cached_match_data == NULL) {
    zsv_pcre2_8_report(errbuf, errbuflen, "Out of memory compiling regular expression");
    pcre2_code_free_8(handle->re);
    free(handle);
    return NULL;
  }

  return handle;
}

/**
 * @brief Implementation of zsv_pcre2_8_new.
 */
regex_handle_t *zsv_pcre2_8_new(const char *pattern) {
  return zsv_pcre2_8_new_ex(pattern, 0, NULL, 0);
}

/**
 * @brief Implementation of zsv_pcre2_8_match.
 */
int zsv_pcre2_8_match(regex_handle_t *handle, const unsigned char *subject, size_t len) {
  int rc = pcre2_match_8(handle->re, (PCRE2_SPTR)subject, len, 0, 0, handle->cached_match_data, NULL);
  if (rc >= 0) // matched
    return 1;
  // Any other error (invalid UTF-8 in the subject, match/depth limit) is reported to
  // the caller as "no match": there is nothing it could do differently. This runs once
  // per cell, so it is latched -- reporting every occurrence would bury a curses UI
  // under thousands of lines on a single non-UTF-8 file, while reporting none would
  // leave `zsv select --regex-search` silently returning zero matches with no signal.
  if (rc != PCRE2_ERROR_NOMATCH && !zsv_pcre2_8_match_error_reported) {
    zsv_pcre2_8_match_error_reported = 1;
    fprintf(stderr, "zsv_pcre2_8_match: match error %i (further occurrences suppressed)\n", rc);
  }
  return 0;
}

/**
 * @brief Implementation of zsv_pcre2_8_has_anchors.
 */
int zsv_pcre2_8_has_anchors(const char *pattern) {
  int in_char_class = 0;

  if (pattern == NULL) {
    return 0;
  }

  for (int i = 0; pattern[i] != '\0'; i++) {
    // First, check for an escape character
    if (pattern[i] == '\\') {
      // Check how many backslashes precede this point
      int backslash_count = 0;
      for (int j = i - 1; j >= 0; j--) {
        if (pattern[j] == '\\') {
          backslash_count++;
        } else {
          break;
        }
      }
      // If we are on an *escaped* backslash (e.g., "\\"),
      // it doesn't escape the *next* character.
      if (backslash_count % 2 == 0) {
        // This backslash is NOT itself escaped, so it *will*
        // escape the next character. Skip the next char.
        i++;
        if (pattern[i] == '\0') {
          // Pattern ended with a backslash
          return 0;
        }
      }
      // If the backslash_count is odd, this backslash *is*
      // escaped (e.g., "\\\^"), so it does NOT escape the
      // next character. We just continue normally.
      continue;
    }

    // We are on a non-escaped character
    if (in_char_class) {
      if (pattern[i] == ']') {
        in_char_class = 0;
      }
      // Inside a char class, all chars are treated as literals
      // or class operators (like -), so we ignore ^ and $
    } else {
      switch (pattern[i]) {
      case '[':
        in_char_class = 1;
        break;
      case '^':
      case '$':
        // Found an unescaped anchor *outside* a char class
        return 1;
      default:
        // Any other character
        break;
      }
    }
  }
  return 0; // No anchors found
}

/**
 * @brief Implementation of zsv_pcre2_8_delete.
 */
void zsv_pcre2_8_delete(regex_handle_t *handle) {
  if (handle) {
    if (handle->cached_match_data)
      pcre2_match_data_free_8(handle->cached_match_data);
    if (handle->re)
      pcre2_code_free_8(handle->re);
    free(handle);
  }
}
