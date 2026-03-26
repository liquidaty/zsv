/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

static int main_version(int argc, const char *argv[]) {
  int verbose = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      verbose = 1;
  }

  printf("zsv version %s (lib %s)\n", VERSION, zsv_lib_version());

  if (verbose) {
    printf("\nBuild configuration:\n");
#if defined(__aarch64__)
    printf("  Fast parser (NEON SIMD):     available\n");
#elif defined(__AVX2__)
    printf("  Fast parser (AVX2 SIMD):     available\n");
#elif defined(__x86_64__) || defined(_M_X64)
    printf("  Fast parser (SSE2 SIMD):     available\n");
#else
    printf("  Fast parser (SIMD):          not available\n");
#endif

#ifdef ZSV_DEFAULT_PARSER_FAST
    printf("  Default parser:              fast\n");
#else
    printf("  Default parser:              compat\n");
#endif

#ifdef ZSV_EXTRAS
    printf("  Extras:                      enabled\n");
#else
    printf("  Extras:                      disabled\n");
#endif

#ifdef ZSV_NO_ONLY_CRLF
    printf("  Only-CRLF row end:           disabled\n");
#else
    printf("  Only-CRLF row end:           available\n");
#endif
  }

  return 0;
}
