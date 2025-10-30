#ifndef ZSV_PCRE2_8_H
#define ZSV_PCRE2_8_H

#include <stddef.h> // for size_t
#include <stdint.h> // for uint32_t

/**
 * @brief An opaque handle to store the compiled regex pattern and context.
 */
typedef struct zsv_pcre2_handle regex_handle_t;

/**
 * @brief Compiles a regex pattern and returns a handle.
 *
 * This function automatically adds PCRE2_UTF and PCRE2_MULTILINE
 * to any options provided. It also pre-configures the match context
 * to treat '\r' (CR) as the only newline character.
 *
 * @param pattern The null-terminated, UTF-8 encoded regex pattern.
 * @param options Additional PCRE2 compile options (e.g., 0, PCRE2_DOTALL).
 * @return A pointer to a regex_handle_t on success, or NULL on error.
 */
regex_handle_t* zsv_pcre2_8_new(const char* pattern, uint32_t options);

/**
 * @brief Checks if a subject string matches a compiled regex pattern.
 *
 * This function assumes the subject is UTF-8 and uses the handle's
 * pre-configured match context (which defines '\r' as the newline).
 *
 * @param handle A handle returned by zsv_pcre2_8_new().
 * @param subject The string to search in (UTF-8 encoded).
 * @param subject_length The length of the subject string in bytes.
 * @return 1 if a match is found, 0 if no match is found or an error occurs.
 */
int zsv_pcre2_8_match(regex_handle_t* handle, const char* subject, size_t subject_length);

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
int zsv_pcre2_8_has_anchors(const char* pattern);

/**
 * @brief Frees the resources associated with a regex handle.
 *
 * @param handle A handle returned by zsv_pcre2_8_new().
 */
void zsv_pcre2_8_delete(regex_handle_t* handle);

#endif // ZSV_PCRE2_8_H

