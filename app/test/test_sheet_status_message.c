/*
 * Unit test of the sheet status message lifetime (app/sheet/status-message.c): the timeout the
 * environment sets, and when a message has had its time
 */
#include <stdio.h>
#include "../sheet/status-message.h"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main(int argc, const char *argv[]) {
  unsigned expected = 0;

  // argv[1] is the timeout this run's ZSVSHEET_STATUS_TIMEOUT_MS must yield; the Makefile
  // covers a plain number, a non-number, a value too large to hold, and the variable unset
  if (argc < 2 || sscanf(argv[1], "%u", &expected) != 1) {
    fprintf(stderr, "usage: %s <expected timeout ms>\n", argv[0]);
    return 1;
  }
  CHECK(zsvsheet_status_message_timeout_ms() == expected);

  // a message has had its time once the timeout has passed, and not a millisecond before
  CHECK(!zsvsheet_status_message_expired(1000, 1000, 4000));
  CHECK(!zsvsheet_status_message_expired(1000, 4999, 4000));
  CHECK(zsvsheet_status_message_expired(1000, 5000, 4000));
  CHECK(zsvsheet_status_message_expired(1000, 5001, 4000));

  // a timeout of 0: a message stays until something replaces it
  CHECK(!zsvsheet_status_message_expired(1, 100000000, 0));
  CHECK(!zsvsheet_status_message_expired(12345, 12345, 0));

  // a status with no clock reports state, not an event: it never expires, however old it is
  CHECK(!zsvsheet_status_message_expired(0, 100000000, 4000));
  CHECK(!zsvsheet_status_message_expired(0, 100000000, 1));

  // a clock that is unavailable (0, per zsv/utils/os.h) or that went backwards leaves the
  // message in place rather than expiring it
  CHECK(!zsvsheet_status_message_expired(1000, 0, 4000));
  CHECK(!zsvsheet_status_message_expired(1000, 999, 4000));

  // a large reading is a large difference, with no wrap in the arithmetic
  CHECK(zsvsheet_status_message_expired(1, 100000000, 4000));

  return 0;
}
