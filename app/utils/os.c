/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/os.h>
#include <stdio.h>
#ifndef _WIN32

void zsv_perror(const char *s) {
  perror(s);
}

#else
#include <windows.h>
#include <strsafe.h>

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

void zsv_win_to_unicode(const void *path, wchar_t *wbuf, size_t wbuf_len) {
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

#include <wchar.h>

int zsv_replace_file(const void *src, const void *dest) {
  wchar_t wdest[PATH_MAX], wsrc[PATH_MAX];

  zsv_win_to_unicode(dest, wdest, ARRAY_SIZE(wdest));
  zsv_win_to_unicode(src, wsrc, ARRAY_SIZE(wsrc));

  if(MoveFileExW(wsrc, wdest, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) // success
    return 0;

  if(GetLastError() == 2) // file not found, could be target. use simple rename
    return _wrename(wsrc, wdest); // returns 0 on success

  return 1; // fail
}


void zsv_win_printLastError() {
  DWORD dw = GetLastError();
  LPVOID lpMsgBuf;
  LPVOID lpDisplayBuf;
  FormatMessage(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                dw,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR) &lpMsgBuf,
                0, NULL );
  // Display the error message and exit the process
  lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
                                    (lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
  StringCchPrintf((LPTSTR)lpDisplayBuf,
                  LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                  TEXT("%s\r\n"), lpMsgBuf);
  fprintf(stderr, "%s\r\n", (LPCTSTR)lpDisplayBuf);
  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
}

void zsv_perror(const char *s) {
  if(s && *s)
    fwrite(s, 1, strlen(s), stderr);
  zsv_win_printLastError(0);
}

#endif
