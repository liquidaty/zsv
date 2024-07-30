/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/dl.h>

#ifdef _WIN32

#include <windows.h>
#define RTLD_LAZY 0
void *dlsym(void* handle, const char* symbol) {
  return (void *)GetProcAddress((HINSTANCE)(handle), (symbol));
}
#endif


void (*zsv_dlsym(void *restrict handle, const char *restrict name))(void) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
  return dlsym(handle, name);
#pragma GCC diagnostic pop
}

#ifdef _WIN32
#include "win/dl.c"
#endif
