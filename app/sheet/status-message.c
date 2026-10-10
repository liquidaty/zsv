// Footer status message lifetime: see status-message.h

#include "status-message.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

// Long enough to read a short sentence, short enough that the footer goes back to its usual hint
#define ZSVSHEET_STATUS_TIMEOUT_MS_DEFAULT 4000

unsigned zsvsheet_status_message_timeout_ms(void) {
  const char *env = getenv("ZSVSHEET_STATUS_TIMEOUT_MS");
  if (env && *env) {
    char *end = NULL;
    errno = 0;
    // a value too large to hold is capped rather than wrapped, or, when the host's unsigned
    // long cannot hold it at all (ERANGE), capped too: the request is "as long as possible"
    unsigned long ms = strtoul(env, &end, 10);
    if (end != env && end && *end == '\0') {
      if (errno == ERANGE)
        return UINT_MAX;
      if (!errno)
        return ms > UINT_MAX ? UINT_MAX : (unsigned)ms;
    }
  }
  return ZSVSHEET_STATUS_TIMEOUT_MS_DEFAULT;
}

int zsvsheet_status_message_expired(unsigned long long set_ms, unsigned long long now_ms, unsigned timeout_ms) {
  if (!set_ms || !timeout_ms || now_ms < set_ms)
    return 0;
  return now_ms - set_ms >= (unsigned long long)timeout_ms;
}
