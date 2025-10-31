#ifndef PCRE2_8_LIB_H
#define PCRE2_8_LIB_H

#include <stddef.h> // For size_t
#include <stdint.h> // For uint32_t

/**
 * @brief An opaque handle for a compiled regex pattern.
 */
typedef struct zsv_pcre2_handle regex_handle_t;

/**
 * @brief Compiles a regex pattern into a handle for matching.
 *
 * This function automatically enables PCRE2_UTF, PCRE2_MULTILINE,
 * and PCRE2_NEWLINE_NUL. This configures the multiline anchors (^ and $)
 * to use '\0' (NULL) as the line delimiter.
 *
 * @param pattern The null-terminated, UTF-8 encoded regex pattern.
 * @param options Additional PCRE2_COMPILE_... options to be OR'd in.
 * @return A pointer to a handle, or NULL on compilation error.
 */
regex_handle_t *zsv_pcre2_8_new(const char *pattern, uint32_t options);

/**
 * @brief Matches a compiled regex handle against a subject string.
 *
 * @param handle A valid handle returned from zsv_pcre2_8_new.
 * @param subject The subject string to search.
 * @param subject_length The length (in bytes) of the subject string.
 * @return 1 if a match is found, 0 otherwise (no match or error).
 */
int zsv_pcre2_8_match(regex_handle_t *handle, const unsigned char *subject, size_t subject_length);

/**
 * @brief Checks if a pattern string contains any unescaped line anchors
 * (^ or $) that are *not* inside a character class.
 *
 * This is a robust-enough check to see if a pattern is attempting
 * line-based matching.
 *
 * @param pattern The null-terminated, UTF-8 encoded regex pattern.
 * @return 1 if a line anchor is found, 0 otherwise.
 */
int zsv_pcre2_8_has_anchors(const char *pattern);

/**
 * @brief Frees the resources associated with a regex handle.
 *
 * @param handle The handle to free.
 */
void zsv_pcre2_8_delete(regex_handle_t *handle);

#endif // PCRE2_8_LIB_H
