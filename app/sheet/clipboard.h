#ifndef ZSVSHEET_CLIPBOARD_H
#define ZSVSHEET_CLIPBOARD_H

#include <stddef.h>

enum zsvsheet_clipboard_status {
  zsvsheet_clipboard_ok = 0,
  zsvsheet_clipboard_error,   // the system refused the copy, or the clipboard tool failed
  zsvsheet_clipboard_no_tool, // no clipboard tool is available on this system
};

// Put text[0..len) on the system clipboard, replacing what was there
enum zsvsheet_clipboard_status zsvsheet_clipboard_put(const char *text, size_t len);

#endif
