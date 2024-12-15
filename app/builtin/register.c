/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

/**
 * Check an extension's implementation for completeness and print any errors or warnings
 * This function does not yet impose any requirements on any extension
 *
 * @param ext the extension to check
 * @return error
 */
static int check_extension(struct zsv_ext *ext) {
  /* to do:
   * check that each extension command supports `zsv my-cmd --help`;
   *   redirect, examine & restore stdin/out/err
   */
  if (!ext)
    return 1;

  ext_init(ext);
  if (ext->inited != zsv_init_ok)
    return 1;
  return 0;
}

static int register_help(char do_register) {
  static const char *register_help = "zsv register: register an extension\n\n"
                                     "usage: zsv register [-g,--global] <extension_id>";
  static const char *unregister_help = "zsv unregister: unregister an extension\n\n"
                                       "usage: zsv unregister [-g,--global] <extension_id>";
  printf("%s\n", do_register ? register_help : unregister_help);
  return 0;
}

static int main_register_aux(int argc, const char *argv[]) {
  int err = 0;
  struct cli_config config;
  char do_register = *argv[0] == 'r';
  const char *extension_id = NULL;
  char global = 0;
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!strcmp(arg, "--help") || !strcmp(arg, "-h"))
      return register_help(do_register);
    if (!strcmp(arg, "-g") || !strcmp(arg, "--global"))
      global = 1;
    else {
      if (extension_id != NULL)
        return register_help(do_register);
      extension_id = arg;
    }
  }

  if (!extension_id)
    fprintf(stderr, "No extension id provided\n"), err = 1;
  else if (strlen(extension_id) < ZSV_EXTENSION_ID_MIN_LEN || strlen(extension_id) > ZSV_EXTENSION_ID_MAX_LEN)
    fprintf(stderr, "Extension id must be 1 to 8 bytes\n"), err = 1;
  else if (config_init_2(&config, !do_register, 1, 1, global, !global))
    config_free(&config); // unable to init config
  else {
    struct zsv_ext *found;
    if ((found = find_extension(&config, extension_id))) {
      // found this extension registered
      if (do_register) {
        fprintf(stderr, "Extension %s already registered\n", extension_id), err = 1;
        check_extension(found);
      }
      // unregister
      else if (!(err = remove_extension(&config.extensions, found)))
        if (!(err = config_save(&config)))
          fprintf(stderr, "Extension %s unregistered\n", extension_id);
    } else {
      // this extension has not been registered
      if (!do_register)
        fprintf(stderr, "Extension %s was not already registered\n", extension_id), err = 1;

      // register
      else {
        // confirm we can successfully load dll, then register
        // add_extension() adds this extension to the front of the list
        if (!(err = add_extension(extension_id, &config.extensions, 1, 1))) {
          if (!check_extension(config.extensions)) {
            if (!(err = config_save(&config)))
              fprintf(stderr, "Extension %s registered\n", extension_id);
          }
        }
      }
    }
    config_free(&config);
  }
  return err;
}

static int main_register(int argc, const char *argv[]) {
  return main_register_aux(argc, argv);
}

static int main_unregister(int argc, const char *argv[]) {
  return main_register_aux(argc, argv);
}
