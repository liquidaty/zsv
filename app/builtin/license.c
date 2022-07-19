/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

static int main_license(int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);

  fprintf(stderr, "Note: for third-party licenses & acknowledgements, run `zsv thirdparty`\n");
  printf("\n====================================================\n");
  printf("ZSV/lib license");
  printf(" ");
  printf("\n====================================================\n");

  fwrite(zsv_license_text_MIT, 1, strlen(zsv_license_text_MIT), stdout);

  struct cli_config config;
  if(!config_init(&config, 0, 1, 0)) {
    for(struct zsv_ext *ext = config.extensions; ext; ext = ext->next) {
      printf("\n====================================================\n");
      printf("License for extension '%s'", ext->id);
      printf("\n====================================================\n");
      if(ext->license && *ext->license) {
        size_t len = strlen(ext->license);
        fwrite(ext->license, 1, len, stdout);
        if(ext->license[len-1] != '\n')
          printf("\n");
      } else
        printf("Unknown\n");
    }
  }
  config_free(&config);

  return 0;
}
