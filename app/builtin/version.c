/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

static int main_version(int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);
  printf("zsv version %s (lib %s)\n", VERSION, zsv_lib_version());
  return 0;
}
