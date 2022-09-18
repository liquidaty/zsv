/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv/utils/string.h>
#include <zsv/utils/os.h>
#include <zsv/utils/dirs.h>

#define INI_HANDLER_LINENO 1
#define INI_CALL_HANDLER_ON_NEW_SECTION 1
#include <ini.h>

/**
 * PREFIX is compile-time option specifying where init data will reside
 * on *x operating systems, when building from source, this should be
 * "/usr/local". For packaged distributions, this should be ""
 * on Windows, this should be either LOCALAPPDATA or APPDATA
 */
#ifndef PREFIX
# if defined(_WIN32)
#  define PREFIX "LOCALAPPDATA"
# elif defined(__EMSCRIPTEN__)
#  define PREFIX "/tmp"
# else
#  define PREFIX "/usr/local"
# endif
#endif

static void write_extension_config(struct zsv_ext *ext, FILE *f) {
  fprintf(f, "[%s]\n\n", ext->id);
}

// config_save: return error
static int config_save(struct cli_config *config) {
  int err = 1;
  char *tmp;
  asprintf(&tmp, "%s.tmp", config->filepath);
  if(!tmp)
    fprintf(stderr, "Out of memory!\n");
  else {
    FILE *f = fopen(tmp, "wb");
    if(!f)
      fprintf(stderr, "Unable to open config file temp %s\n", tmp);
    else {
      for(struct zsv_ext *ext = config->extensions; ext; ext = ext->next)
        write_extension_config(ext, f);
      fclose(f);
      if((err = zsv_replace_file(tmp, config->filepath)))
        perror("Unable to update config file");
    }
    free(tmp);
  }
  return err;
}

static enum zsv_ext_status zsv_ext_delete(struct zsv_ext *ext);

static int remove_extension(struct zsv_ext **list, struct zsv_ext *element) {
  char found = 1;
  for( ; *list; list = &(*list)->next) {
    if(*list == element) {
      *list = element->next;
      found = 1;
      break;
    }
  }
  if(found) {
    element->next = NULL;
    return zsv_ext_delete(element);
  }
  return 1;
}

static struct zsv_ext *find_extension(struct cli_config *config, const char *id) {
  for(struct zsv_ext *ext = config ? config->extensions : NULL; ext; ext = ext->next)
    if(!zsv_stricmp((const unsigned char *)ext->id, (const unsigned char *)id))
      return ext;
  return NULL;
}

static char extension_id_ok(const unsigned char *id) {
  for(int i = 0; id[i]; i++)
    if(!(
         (id[i] >= 'a' && id[i] <= 'z')
         || (id[i] >= '0' && id[i] <= '9')
         )
       )
      return 0;
  return 1;
}

// load an extension
static struct zsv_ext *load_extension_dl(const unsigned char *extension_id, char verbose) {
  char *extension_name;
  struct zsv_ext *ext = NULL;
  asprintf(&extension_name, "zsvext%s.%s", extension_id, DL_SUFFIX);
  if(!extension_name)
    fprintf(stderr, "Out of memory!\n");
  else if((ext = zsv_ext_new(extension_name, (const char *)extension_id, verbose))) {
    if(verbose)
      fprintf(stderr, "Loaded extension %s\n", extension_name);
    free(extension_name);
  }
  return ext;
}

// load an extension and if successful, add to config->extensions head
static int add_extension(const char *id, struct zsv_ext **exts, char ignore_err, char verbose) {
  int err = 0;
  size_t len = 2;
  unsigned char *extension_id = zsv_strtolowercase((const unsigned char *)id, &len);
  if(extension_id) {
    struct zsv_ext *ext = NULL;
    if(!extension_id_ok(extension_id))
      fprintf(stderr, "Invalid extension id: %s\n", extension_id), err = 1;
    else if(!(ext = load_extension_dl(extension_id, verbose)))
      fprintf(stderr, "Unexpected error loading extension %s\n", extension_id);
    else {
      if(!ignore_err && !(ext && ext->ok)) {
        fprintf(stderr, "Invalid extension: %s\n", extension_id), err = 1;
        zsv_ext_delete(ext);
      } else {
        ext->next = *exts;
        *exts = ext;
      }
    }
    free(extension_id);
  }
  return err;
}

// get_config_file(): return zsv.ini
static int config_ini_handler(void* ctx, const char* section,
                              const char* name, const char* value,
                              int lineno) {
  int err = 0;
  struct cli_config *config = ctx;
  if(section) {
    if(!name && !value) { // initialize section
      if(zsv_stricmp((const unsigned char *)section, (const unsigned char *)"default")) {
        if(strlen(section) != 2) {
          fprintf(stderr, "Invalid extension id: %s\n", section);
          err = 1;
        } else {
          struct zsv_ext *ext = find_extension(config, section);
          if(!ext && add_extension(section, &config->extensions, 1, config->verbose)) {
            if(config->err_if_not_found) {
              err = 1;
              fprintf(stderr, "At line %i in %s\n", lineno, config->filepath);
            }
          }
        }
      }
    }
  }
  return err ? 0 : 1;
}

static size_t get_ini_file(char *buff, size_t buffsize) {
  size_t len = zsv_get_config_dir(buff, buffsize, PREFIX);
  if(len) {
    size_t n = snprintf(buff + len, buffsize - len, "%czsv.ini", FILESLASH);
    if(n > 0 && n + len < buffsize)
      return len + n;
  }
  return 0;
}

// parse_extensions_ini: return -2 (unexpected), -1 (file open), 0 (success), > 0 (line number)
static int parse_extensions_ini(struct cli_config *config, char err_if_not_found, char verbose) {
  int err = 0;
  FILE *f;
  if(!(f = fopen(config->filepath, "r"))) {
    if(err_if_not_found) {
      err = -1;
      fprintf(stderr, "No extensions configured%s%s\n",
              verbose ? "or file not found: " : "",
              verbose ? config->filepath : "");
    }
  } else {
    config->err_if_not_found = err_if_not_found;
    config->verbose = verbose;
    err = ini_parse_file(f, config_ini_handler, config);
    fclose(f);
    if(err > 0)
      fprintf(stderr, "Error parsing %s on line %i\n", config->filepath, err);
  }
  return err;
}

static struct zsv_ext_command *ext_command_new(const char *id, const char *help,
                                               enum zsv_ext_status (*extmain)(zsv_execution_context ctx, int argc, const char *argv[])
                                               ) {
  struct zsv_ext_command *cmd = calloc(1, sizeof(*cmd));
  cmd->id = strdup(id ? id : "");
  cmd->help = help ? strdup(help) : NULL;
  cmd->main = extmain;
  return cmd;
}

static void ext_command_delete(struct zsv_ext_command *cmd) {
  if(cmd) {
    free(cmd->id);
    free(cmd->help);
    free(cmd);
  }
}
