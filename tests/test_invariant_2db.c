#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include "app/2db.c"

START_TEST(test_allocation_size_overflow_protection)
{
    // Invariant: Multiplication for allocation size must not overflow or must be validated
    const struct {
        size_t width;
        size_t height;
        const char *description;
    } test_cases[] = {
        {SIZE_MAX, 2, "Exploit case: multiplication wraps to small value"},
        {SIZE_MAX / 2 + 1, 2, "Boundary case: just overflows"},
        {100, 100, "Valid case: normal operation"}
    };
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        // The actual function from 2db.c should handle overflow safely
        // We're testing that the function either detects overflow or allocates correctly
        size_t width = test_cases[i].width;
        size_t height = test_cases[i].height;
        
        // Call the actual allocation function - assuming it's named allocate_buffer
        // If the function name is different, replace allocate_buffer with the actual function name
        void *result = allocate_buffer(width, height);
        
        // Security property: either allocation succeeds with valid size,
        // or overflow is detected and handled safely (e.g., returns NULL, aborts, etc.)
        // We can't assert specific behavior without knowing the implementation,
        // but we can verify the function doesn't crash and handles edge cases
        if (result != NULL) {
            // If allocation succeeded, verify it's not a dangerously small buffer
            // by checking if width * height would overflow
            if (width > 0 && height > 0 && width > SIZE_MAX / height) {
                // Multiplication would overflow - allocation should have failed
                ck_assert_msg(0, "Allocation succeeded when multiplication would overflow");
            }
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_allocation_size_overflow_protection);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}