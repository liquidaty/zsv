#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <stdio.h>
#include <stdlib.h>
#include "pcre2-8.h"

/**
 * @brief Defines the internal structure of the handle.
 * We only need to store the compiled code.
 */
struct zsv_pcre2_handle {
  pcre2_code *re;
  pcre2_match_data *cached_match_data;
};

/**
 * @brief Implementation of zsv_pcre2_8_new.
 */
regex_handle_t *zsv_pcre2_8_new(const char *pattern, uint32_t options) {
  regex_handle_t *handle = calloc(1, sizeof(regex_handle_t));
  if (handle == NULL) {
    perror("zsv_pcre2_8_new");
    return NULL;
  }

  int error_number;
  PCRE2_SIZE error_offset;

  // utf-8 and multiline support
  uint32_t compile_options = options | PCRE2_UTF | PCRE2_MULTILINE;

  pcre2_compile_context *compile_context = pcre2_compile_context_create_8(NULL);
  if (compile_context == NULL) {
    printf("Error: Failed to create compile context.\n");
    free(handle);
    return NULL;
  }

  // set newline convention to '\0'
  pcre2_set_newline_8(compile_context, PCRE2_NEWLINE_NUL);

  handle->re = pcre2_compile_8((PCRE2_SPTR)pattern,   // the pattern
                               PCRE2_ZERO_TERMINATED, // pattern is zero-terminated
                               compile_options,       // user options + UTF + MULTILINE
                               &error_number,         // for error number
                               &error_offset,         // for error offset
                               compile_context        // use our configured compile context
  );

  // free the compile context (no longer needed)
  pcre2_compile_context_free_8(compile_context);

  if (handle->re == NULL) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message_8(error_number, buffer, sizeof(buffer));
    printf("PCRE2 compilation failed for pattern \"%s\" at offset %d: %s\n", pattern, (int)error_offset, buffer);
    free(handle);
    return NULL;
  }

  handle->cached_match_data = pcre2_match_data_create_from_pattern_8(handle->re, NULL);

  return handle;
}

/**
 * @brief Implementation of zsv_pcre2_8_match.
 */
int zsv_pcre2_8_match(regex_handle_t *handle, const unsigned char *subject, size_t len) {
  int rc = pcre2_match_8(handle->re, (PCRE2_SPTR)subject, len, 0, 0, handle->cached_match_data, NULL);
  if (rc >= 0) // matched
    return 1;
  if (rc != PCRE2_ERROR_NOMATCH)
    fprintf(stderr, "zsv_pcre2_8_match: match error %i\n", rc);
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
