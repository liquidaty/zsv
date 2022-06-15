/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifdef _WIN32
#include <zsv/utils/os.h>
#include <windows.h>

static void strlcpy(register char *dst, register const char *src, size_t n) {
  for (; *src != '\0' && n > 1; n--) {
    *dst++ = *src++;
  }
  *dst = '\0';
}

static void change_slashes_to_backslashes(char *path) {
  int i;
  for (i = 0; path[i] != '\0'; i++) {
    if (path[i] == '/') {
      path[i] = '\\';
    }
    if ((path[i] == '\\') && (i > 0)) {
      while (path[i + 1] == '\\' || path[i + 1] == '/') {
        (void)memmove(path + i + 1, path + i + 2, strlen(path + i + 1));
      }
    }
  }
}

void to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len) {
  char buf[PATH_MAX], buf2[PATH_MAX];
  strlcpy(buf, path, sizeof(buf));

  change_slashes_to_backslashes(buf);

  /* Convert to Unicode and back. If doubly-converted string does not
   * match the original, something is fishy, reject. */
  memset(wbuf, 0, wbuf_len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, (int)wbuf_len);
  WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)wbuf_len, buf2,
                      sizeof(buf2), NULL, NULL);
  if (strcmp(buf, buf2) != 0) {
    wbuf[0] = L'\0';
  }
}

#include <stdio.h>
#include <wchar.h>
int zsv_replace_file(const void *src, const void *dest) {
  wchar_t wdest[PATH_MAX], wsrc[PATH_MAX];
  to_unicode(dest, wdest, ARRAY_SIZE(wdest));
  to_unicode(src, wsrc, ARRAY_SIZE(wsrc));

  if(ReplaceFileW(wdest, wsrc, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, 0, 0)) // success
    return 0;
  else if(GetLastError() == 2) // file not found, could be target. use simple rename
    return _wrename(wsrc, wdest); // returns 0 on success
  return 1; // fail
}
#endif
