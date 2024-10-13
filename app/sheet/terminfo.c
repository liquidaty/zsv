#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// check_or_set_terminfo: return 1 if ok, 0 if not
static int terminfo_ok(void) {
  // Check if TERMINFO environment variable is set
  char *terminfo_env = getenv("TERMINFO");
  if (terminfo_env != NULL)
    return 1; // ok

  // Check some default locations
  const char *default_paths[] = {
    "/usr/share/terminfo", "/lib/terminfo", "/usr/lib/terminfo", "/etc/terminfo", "/usr/local/share/terminfo", NULL};

  for (int i = 0; default_paths[i] != NULL; i++) {
    if (access(default_paths[i], R_OK) == 0) {
      // Set the TERMINFO environment variable
      if (setenv("TERMINFO", default_paths[i], 1) == 0)
        return 1; // ok
      else {
        perror("Failed to set TERMINFO environment variable");
        return 0;
      }
    }
  }
#if defined(WIN32) || defined(_WIN32)
  return 1; // ok
#endif
  return 0;
}
