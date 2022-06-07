/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

static void print_str_array(const char *name, const char *name2, const char **ss) {
  printf("\n\n==========================\n%s%s\n==========================\n", name, name2 ? name2 : "?");
  if(!ss)
    printf("No third-party information\n");
  for(int i = 0; ss && ss[i]; i++) {
    const char *s = ss[i];
    fwrite(s, 1, strlen(s), stdout);
  }
}

static int main_thirdparty(int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);

  printf("Third-party licenses and acknowldgements");
  print_str_array("ZSV/lib third-party dependencies", "", zsv_thirdparty);

  struct cli_config config;
  const char *ss[2];
  ss[1] = NULL;
  if(!config_init(&config, 0, 1, 0)) {
    for(struct zsv_ext *ext = config.extensions; ext; ext = ext->next) {
      if(ext->thirdparty) {
        ss[0] = ext->thirdparty;
        print_str_array("Extension: ", (const char *)ext->id, ss);
      }
    }
  }
  printf("\n");
  return 0;
}
