/*
 * Regression test for integer underflow vulnerability in fast CSV scanner
 *
 * Tests for CVE-style vulnerability where malformed CSV inputs < 64 bytes
 * could cause cell_start > current_position, leading to integer underflow
 * in length calculations and subsequent heap buffer overflow.
 *
 * This test should PASS when the vulnerability is fixed (exit 0)
 * and FAIL when the vulnerability is present (exit 1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

// Include the zsv library headers to test the API
#include <zsv.h>

static jmp_buf crash_jmp;
static volatile int received_signal = 0;

// Signal handler to catch segfaults/aborts
void signal_handler(int sig) {
  received_signal = sig;
  longjmp(crash_jmp, 1);
}

// Row handler that detects vulnerability indicators
void vulnerable_test_row_handler(void *ctx) {
  zsv_parser parser = (zsv_parser)ctx;
  size_t n = zsv_cell_count(parser);

  for (size_t i = 0; i < n; i++) {
    struct zsv_cell c = zsv_get_cell(parser, i);

    // Detect suspiciously large cell lengths that indicate underflow
    if (c.len > 65536) { // Reasonable threshold for detecting underflow
      printf("VULNERABILITY DETECTED: Cell %zu has suspicious length %zu\n", i, c.len);
      exit(1); // Test FAILS - vulnerability present
    }
  }
}

int test_vulnerability_payload() {
  printf("Testing vulnerability payload...\n");

  // The specific payload that triggers the vulnerability
  unsigned char payload[] = {0x32, 0x02, 0x61, 0x2c, 0x22, 0x2c, 0x3c, 0x22, 0x0a, 0x22};
  size_t payload_len = sizeof(payload);

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.scan_engine = 3; // ZSV_MODE_DELIM_FAST - where vulnerability exists
  opts.max_columns = 256;
  opts.max_row_size = 4096;

  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    printf("Failed to create parser\n");
    return 1;
  }

  zsv_set_row_handler(parser, vulnerable_test_row_handler);
  zsv_set_context(parser, parser);

  // Install signal handlers to catch crashes
  signal(SIGSEGV, signal_handler);
  signal(SIGBUS, signal_handler);
  signal(SIGABRT, signal_handler);

  if (setjmp(crash_jmp) != 0) {
    printf("VULNERABILITY DETECTED: Received signal %d (likely crash)\n", received_signal);
    zsv_delete(parser);
    return 1; // Test FAILS - vulnerability caused crash
  }

  // Parse the payload
  zsv_parse_bytes(parser, payload, payload_len);
  zsv_finish(parser); // Critical: vulnerability occurs in finish()

  zsv_delete(parser);
  printf("Payload processed without crash or suspicious cell lengths\n");
  return 0; // Test PASSES - vulnerability not present
}

int test_normal_csv() {
  printf("Testing normal CSV to ensure we didn't break functionality...\n");

  char *normal_csv = "name,age\nAlice,25\nBob,30\n";

  struct zsv_opts opts;
  memset(&opts, 0, sizeof(opts));
  opts.scan_engine = 3; // Test fast parser with normal data

  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    printf("Failed to create parser for normal test\n");
    return 1;
  }

  zsv_set_row_handler(parser, vulnerable_test_row_handler);
  zsv_set_context(parser, parser);

  signal(SIGSEGV, signal_handler);
  signal(SIGBUS, signal_handler);
  signal(SIGABRT, signal_handler);

  if (setjmp(crash_jmp) != 0) {
    printf("UNEXPECTED: Normal CSV caused crash (signal %d)\n", received_signal);
    zsv_delete(parser);
    return 1;
  }

  zsv_parse_bytes(parser, (unsigned char *)normal_csv, strlen(normal_csv));
  zsv_finish(parser);

  zsv_delete(parser);
  printf("Normal CSV processed correctly\n");
  return 0;
}

int main() {
  printf("=== ZSV Integer Underflow Vulnerability Regression Test ===\n");

  // Test 1: Try to trigger the vulnerability
  if (test_vulnerability_payload() != 0) {
    printf("FAIL: Vulnerability test failed\n");
    return 1;
  }

  // Test 2: Make sure normal functionality still works
  if (test_normal_csv() != 0) {
    printf("FAIL: Normal CSV test failed\n");
    return 1;
  }

  printf("PASS: All vulnerability tests passed\n");
  return 0;
}