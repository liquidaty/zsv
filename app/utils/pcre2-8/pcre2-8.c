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
};

/**
 * @brief Implementation of zsv_pcre2_8_new.
 */
regex_handle_t* zsv_pcre2_8_new(const char* pattern, uint32_t options) {
    regex_handle_t *handle = malloc(sizeof(regex_handle_t));
    if (handle == NULL) {
        printf("Error: Failed to allocate memory for handle.\n");
        return NULL;
    }
    handle->re = NULL;

    int error_number;
    PCRE2_SIZE error_offset;

    // Force UTF-8, Multiline support, and NUL as newline
    uint32_t compile_options = options | PCRE2_UTF | PCRE2_MULTILINE | PCRE2_NEWLINE_NUL;

    handle->re = pcre2_compile_8(
        (PCRE2_SPTR)pattern,   // the pattern
        PCRE2_ZERO_TERMINATED, // pattern is zero-terminated
        compile_options,       // user options + UTF + MULTILINE + NUL
        &error_number,         // for error number
        &error_offset,         // for error offset
        NULL                   // use default compile context
    );

    if (handle->re == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message_8(error_number, buffer, sizeof(buffer));
        printf("PCRE2 compilation failed for pattern \"%s\" at offset %d: %s\n",
               pattern, (int)error_offset, buffer);
        free(handle);
        return NULL;
    }

    return handle;
}

/**
 * @brief Implementation of zsv_pcre2_8_match.
 */
int zsv_pcre2_8_match(regex_handle_t* handle, const unsigned char* subject, size_t subject_length) {
    // Removed check for handle->match_context
    if (handle == NULL || handle->re == NULL || subject == NULL) {
        return 0; // Invalid input
    }

    pcre2_match_data *match_data;
    int rc;
    int result = 0;

    // 1. Create a match data block
    match_data = pcre2_match_data_create_from_pattern_8(handle->re, NULL);
    if (match_data == NULL) {
        printf("Error: Failed to create match data block.\n");
        return 0;
    }

    // 2. Run the match. Pass NULL for the match context.
    // The newline behavior is now compiled into handle->re.
    rc = pcre2_match_8(
        handle->re,                // the compiled pattern
        (PCRE2_SPTR)subject,       // the subject string
        subject_length,            // the length of the subject
        0,                         // start at offset 0
        0,                         // default options
        match_data,                // block for storing match data
        NULL                       // use default match context
    );

    // 3. Check result
    if (rc >= 0) {
        result = 1; // Match found
    } else if (rc != PCRE2_ERROR_NOMATCH) {
        // An error occurred (other than no match)
        printf("Matching error %d\n", rc);
    }
    // If rc == PCRE2_ERROR_NOMATCH, result remains 0 (no match)

    // 4. Free match data
    pcre2_match_data_free_8(match_data);
    return result;
}

/**
 * @brief Implementation of zsv_pcre2_8_has_anchors.
 */
int zsv_pcre2_8_has_anchors(const char* pattern) {
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
void zsv_pcre2_8_delete(regex_handle_t* handle) {
    if (handle != NULL) {
        if (handle->re != NULL) {
            pcre2_code_free_8(handle->re);
        }
        // Removed free for handle->match_context
        free(handle);
    }
}
