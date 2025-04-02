#include <stdbool.h> // For bool, true, false
#include <stddef.h>  // For size_t
#include <ctype.h>   // For isdigit

/**
 * @brief Checks if a buffer segment represents a valid JSON number (RFC 8259 compliant).
 *
 * Follows RFC 8259 Section 6 rules strictly. Numbers MUST have an integer part.
 * Operates on a character buffer `str` of explicit `length`.
 * Assumes `str` is NOT null-terminated and reads exactly `length` bytes.
 * Accessing memory beyond `str + length - 1` is avoided.
 *
 * JSON Number Format (RFC 8259):
 * number = [ minus ] int [ frac ] [ exp ]
 * int = zero / ( digit1-9 *DIGIT )
 * frac = decimal-point 1*DIGIT
 * exp = e [ minus / plus ] 1*DIGIT
 *
 * @param str Pointer to the start of the character buffer.
 * @param length The exact number of characters in the buffer segment to validate.
 * @return true if the buffer segment [str, str + length) is a valid JSON number
 * per RFC 8259, false otherwise.
 */
bool is_valid_json_number(const char *str, size_t length) {
    // 1. Handle NULL pointer or Zero Length
    if (str == NULL || length == 0) {
        return false;
    }

    const char *p = str;
    const char * const end = str + length; // Pointer to one position *after* the last valid char

    // 2. Optional Minus Sign
    // Check bounds before dereferencing
    if (p < end && *p == '-') {
        p++; // Advance pointer
    }

    // 3. Integer Part (Mandatory per RFC 8259)
    // Check if we are already at the end (e.g., input was "-" or empty after sign)
    if (p >= end) {
         return false;
    }

    if (*p == '0') {
        p++; // Consumed '0'
        // Check bounds *before* looking at the next character for leading zero violation
        if (p < end && isdigit((unsigned char)*p)) {
             return false; // Leading zero rule violation (e.g., "01")
        }
        // '0' must be followed by '.', 'e', 'E', or the end of the buffer.
    } else if (isdigit((unsigned char)*p)) { // Starts with 1-9
        p++; // Consume the first digit (1-9)
        // Consume subsequent digits, checking bounds in the loop
        while (p < end && isdigit((unsigned char)*p)) {
            p++;
        }
    } else {
        // Did not start with '0' or '1'-'9' after optional sign. Invalid.
        // Correctly rejects ".2", "-.2" etc. even with length check.
        return false;
    }
    // If we reached here, a valid integer part was parsed up to the current 'p'.

    // 4. Optional Fractional Part
    // Check bounds before checking for '.'
    if (p < end && *p == '.') {
        p++; // Consumed '.'
        // Check bounds *before* looking for the mandatory digit after '.'
        if (p >= end || !isdigit((unsigned char)*p)) {
             // Reached end immediately after '.', or next char not a digit (e.g., "1.", "0.")
             return false;
        }
        p++; // Consumed the first digit after '.'
        // Consume subsequent digits, checking bounds in the loop
        while (p < end && isdigit((unsigned char)*p)) {
             p++;
        }
    }

    // 5. Optional Exponent Part
    // Check bounds before checking for 'e'/'E'
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++; // Consumed 'e' or 'E'
        // Check bounds before checking for optional sign
        if (p < end && (*p == '+' || *p == '-')) {
            p++; // Consumed optional sign
        }
        // Check bounds *before* looking for the mandatory digit after exponent part
        if (p >= end || !isdigit((unsigned char)*p)) {
             // Reached end immediately after 'e'/'E'/sign, or next char not a digit (e.g., "1e", "1E+")
             return false;
        }
        p++; // Consumed the first digit after exponent part
        // Consume subsequent digits, checking bounds in the loop
        while (p < end && isdigit((unsigned char)*p)) {
             p++;
        }
    }

    // 6. Final Check: Must have consumed exactly 'length' characters
    // If p == end, we successfully processed all characters from str to str + length - 1
    // according to the JSON number rules.
    return p == end;
}

#ifdef JSON_NUMERIC_TEST

// --- Example Usage ---
#include <stdio.h>
#include <string.h> // For strlen in test driver ONLY

// Updated test function to use length
void test_json_number_len(const char* str, size_t len) {
     // Create a temporary non-null terminated buffer for printing if needed,
     // or just print the pointer and length. Be careful printing non-terminated strings.
     // Using printf("%.*s", (int)len, str) is safer for printing.
    printf("\"%.*s\" (len %zu) -> %s\n", (int)len, str ? str : "(null)", len,
           is_valid_json_number(str, len) ? "VALID" : "INVALID");
}

// Helper for testing null-terminated strings using the length-based function
void test_json_number_nul(const char* str) {
    if (str == NULL) {
        printf("NULL (len 0) -> %s\n", is_valid_json_number(NULL, 0) ? "VALID" : "INVALID");
        return;
    }
    size_t len = strlen(str); // Use strlen JUST for the test case driver
    test_json_number_len(str, len);
}


int main() {
    printf("--- Testing with NUL-terminated strings ---\n");
    printf("--- Valid Examples (RFC 8259 Compliant) ---\n");
    test_json_number_nul("0");
    test_json_number_nul("1");
    test_json_number_nul("-1");
    test_json_number_nul("12345");
    test_json_number_nul("0.5");
    test_json_number_nul("1.234");
    test_json_number_nul("1e5");
    test_json_number_nul("1E-5");
    test_json_number_nul("1.2E+10");
    test_json_number_nul("-0.123e-4");

    printf("\n--- Invalid Examples (RFC 8259 Compliant) ---\n");
    test_json_number_nul(NULL);
    test_json_number_nul("");
    test_json_number_nul("-");
    test_json_number_nul(".");
    test_json_number_nul(".2");
    test_json_number_nul("1.");
    test_json_number_nul("01");
    test_json_number_nul("1e");
    test_json_number_nul("1e+");
    test_json_number_nul("1.2a");
    test_json_number_nul("1.2e5 "); // Trailing space

    printf("\n--- Testing with specific lengths ---\n");
    test_json_number_len("12345", 3);   // "123" -> VALID (Consumes exactly 3)
    test_json_number_len("12a45", 2);   // "12" -> VALID (Consumes exactly 2)
    test_json_number_len("12a45", 3);   // "12a" -> INVALID (a is not part of num, doesn't consume exactly 3 as num)
    test_json_number_len("1.5x", 3);    // "1.5" -> VALID (Consumes exactly 3)
    test_json_number_len("1.5x", 4);    // "1.5x" -> INVALID (x is extra)
    test_json_number_len("1.", 1);      // "1" -> VALID (Consumes exactly 1)
    test_json_number_len("1.", 2);      // "1." -> INVALID (Needs digit after .)
    test_json_number_len("-", 1);       // "-" -> INVALID (No digits)
    test_json_number_len("-1", 1);      // "-" -> INVALID (Only consumes 1, needs the digit too)
    test_json_number_len("-1", 2);      // "-1" -> VALID (Consumes exactly 2)
    test_json_number_len("0", 1);       // "0" -> VALID
    test_json_number_len("0", 0);       // "" -> INVALID (Zero length handled)
    test_json_number_len(NULL, 0);      // NULL -> INVALID

    return 0;
}
#endif
