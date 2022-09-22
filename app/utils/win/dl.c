/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifdef _WIN32
#include <windows.h>
#include <zsv/utils/os.h>

void *dlopen(const char *dll_name, int flags) {
  (void)flags;
  wchar_t wbuf[PATH_MAX];
  zsv_win_to_unicode(dll_name, wbuf, ARRAY_SIZE(wbuf));
  return LoadLibraryW(wbuf);
}

int dlclose(void *handle) {
  int result;
  if (FreeLibrary((HMODULE)handle) != 0)
    result = 0;
  else
    result = -1;
  return result;
}
#endif
