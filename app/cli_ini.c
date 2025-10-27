/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */
#include <errno.h>
#include <zsv/utils/string.h>
#include <zsv/utils/os.h>
#include <zsv/utils/file.h>
#include <zsv/utils/dirs.h>
#include <zsv/utils/mem.h>

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
#if defined(_WIN32)
#define PREFIX "LOCALAPPDATA"
#elif defined(__EMSCRIPTEN__)
#define PREFIX "/tmp"
#else
#define PREFIX "/usr/local"
#endif
#endif

static void write_extension_config(struct zsv_ext *ext, FILE *f) {
  fprintf(f, "[%s]\n\n", ext->id);
}

// config_save: return error
static int config_save(struct cli_config *config) {
  const char *config_filepath = NULL;
  if (*config->filepath)
    config_filepath = config->filepath;
  if (*config->home_filepath) {
    if (config_filepath) {
      fprintf(stderr, "config_save: only home or global config filepath should exist, but got both\n");
      return 1;
    }
    config_filepath = config->home_filepath;
  }
  if (!config_filepath) {
    fprintf(stderr, "config_save: no home or global filepath found\n");
    return 1;
  }
  int err = 1;
  char *tmp = zsv_get_temp_filename("zsv_config_XXXXXXXX");
  if (!tmp)
    fprintf(stderr, "Out of memory!\n");
  else {
    FILE *f = fopen(tmp, "wb");
    if (!f)
      perror(tmp);
    else {
      for (struct zsv_ext *ext = config->extensions; ext; ext = ext->next)
        write_extension_config(ext, f);
      fclose(f);
      if ((err = zsv_replace_file(tmp, config_filepath)))
        perror(config_filepath);
      else if (config_filepath != config->home_filepath) {
        // try to change permissions to allow anyone to read
        mode_t new_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        if (chmod(config_filepath, new_permissions) != 0) {
          ; // maybe handle error here
        }
      }
    }
    free(tmp);
  }
  return err;
}

static enum zsv_ext_status zsv_ext_delete(struct zsv_ext *ext);

static int remove_extension(struct zsv_ext **list, struct zsv_ext *element) {
  char found = 1;
  for (; *list; list = &(*list)->next) {
    if (*list == element) {
      *list = element->next;
      found = 1;
      break;
    }
  }
  if (found) {
    element->next = NULL;
    return zsv_ext_delete(element);
  }
  return 1;
}

static struct zsv_ext *find_extension(struct cli_config *config, const char *id) {
  for (struct zsv_ext *ext = config ? config->extensions : NULL; ext; ext = ext->next)
    if (!zsv_stricmp((const unsigned char *)ext->id, (const unsigned char *)id))
      return ext;
  return NULL;
}

static char extension_id_ok(const unsigned char *id) {
  for (int i = 0; id[i]; i++)
    if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9')))
      return 0;
  return 1;
}

// load an extension
static struct zsv_ext *load_extension_dl(const unsigned char *extension_id, char verbose) {
  char *extension_name;
  struct zsv_ext *ext = NULL;
  asprintf(&extension_name, "zsvext%s.%s", extension_id, DL_SUFFIX);
  if (!extension_name)
    fprintf(stderr, "Out of memory!\n");
  else if ((ext = zsv_ext_new(extension_name, (const char *)extension_id, verbose))) {
    if (verbose && ext->dl)
      fprintf(stderr, "Loaded extension %s\n", extension_name);
    free(extension_name);
  }
  return ext;
}

// load an extension and if successful, add to config->extensions head
static int add_extension(const char *id, struct zsv_ext **exts, char ignore_err, char verbose) {
  int err = 0;
  const char *dash = strchr(id, '-');
  unsigned char *extension_id = NULL;
  size_t len;
  if (dash)
    len = dash - id;
  else
    len = strlen(id);
  extension_id = zsv_strtolowercase((const unsigned char *)id, &len);
  if (extension_id && len) {
    struct zsv_ext *ext = NULL;
    if (!extension_id_ok(extension_id))
      fprintf(stderr, "Invalid extension id: %s\n", extension_id), err = 1;
    else if (!(ext = load_extension_dl(extension_id, verbose)))
      fprintf(stderr, "Unexpected error loading extension %s\n", extension_id);
    else {
      if (!ignore_err && !(ext && ext->ok)) {
        fprintf(stderr, "Invalid extension: %s\n", extension_id), err = 1;
        zsv_ext_delete(ext);
      } else {
        ext->next = *exts;
        *exts = ext;
      }
    }
  }
  free(extension_id);
  return err;
}

static int config_ini_handler(void *ctx, const char *section, const char *name, const char *value, int lineno) {
  int err = 0;
  struct cli_config *config = ctx;
  if (section) {
    if (!name && !value) { // initialize section
      if (zsv_stricmp((const unsigned char *)section, (const unsigned char *)"default")) {
        if (!(strlen(section) >= ZSV_EXTENSION_ID_MIN_LEN && strlen(section) <= ZSV_EXTENSION_ID_MAX_LEN)) {
          fprintf(stderr, "Invalid extension id: %s. Length must be between %i and %i\n", section,
                  ZSV_EXTENSION_ID_MIN_LEN, ZSV_EXTENSION_ID_MAX_LEN);
          err = 1;
        } else {
          struct zsv_ext *ext = find_extension(config, section);
          if (!ext && add_extension(section, &config->extensions, 1, config->verbose)) {
            if (config->err_if_not_found) {
              err = 1;
              fprintf(stderr, "At line %i in %s\n", lineno, config->current_filepath);
            }
          }
        }
      }
    }
  }
  return err ? 0 : 1;
}

// get_config_file(): return non-zero on success
static size_t get_ini_file(char *buff, size_t buffsize, char global) {
  size_t len = 0;
  int n = 0;
  if (global) {
    len = zsv_get_config_dir(buff, buffsize, PREFIX);
    if (len > 0)
      n = snprintf(buff + len, buffsize - len, "%czsv", FILESLASH);
  } else {
    n = zsv_get_home_dir(buff, buffsize);
    if (n > 0 && (size_t)n < buffsize) {
      len = (size_t)n;
      n = snprintf(buff + len, buffsize - len, "%c.config%czsv", FILESLASH, FILESLASH);
    }
  }
  if (len > 0) {
    if (n > 0 && (size_t)n + len < buffsize)
      len += (size_t)n;
    else
      len = 0;
  }
  if (len && buffsize > len) {
    int dir_exists = zsv_dir_exists(buff);
    if (!dir_exists) {
      if (zsv_mkdirs(buff, 0)) // unable to create the config file parent dir
        perror(buff);
      else if (global) {
        dir_exists = 1;
        // try to make dir read-only for all
        // Set permissions to rwxr-xr-x (0755)
        mode_t new_permissions = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        chmod(buff, new_permissions); // to do: maybe handle error?
      }
    }
    if (dir_exists) {
      n = snprintf(buff + len, buffsize - len, "%czsv.ini", FILESLASH);
      if (n > 0 && (size_t)n + len < buffsize)
        return len + (size_t)n;
    }
  }
  return 0;
}

static int parse_extensions_ini_file(struct cli_config *config, const char *path, int *opened_count) {
  int err = 0;
  if (path && *path) {
    FILE *f = fopen(path, "r");
    if (!f)
      err = errno;
    else {
      (*opened_count)++;
      config->current_filepath = path;
      err = ini_parse_file(f, config_ini_handler, config);
      fclose(f);
      if (err > 0)
        fprintf(stderr, "Error parsing %s on line %i\n", config->current_filepath, err);
    }
  }
  return err;
}

// parse_extensions_ini: return -2 (unexpected), -1 (file open), 0 (success), > 0 (line number)
static int parse_extensions_ini(struct cli_config *config, char err_if_not_found, char verbose) {
  int err = 0;
  int opened_config_files = 0;
  char old_err_if_not_found = config->err_if_not_found;
  char old_verbose = config->verbose;
  config->err_if_not_found = err_if_not_found;
  config->verbose = verbose;
  parse_extensions_ini_file(config, config->home_filepath, &opened_config_files);
  parse_extensions_ini_file(config, config->filepath, &opened_config_files);
  if (err_if_not_found && opened_config_files == 0) {
    err = -1;
    fprintf(stderr, "No extensions configured%s%s%s%s\n", verbose ? " or file(s) not found:\n  " : "",
            verbose ? config->home_filepath : "", verbose ? "\n  " : "", verbose ? config->filepath : "");
  }
  config->err_if_not_found = old_err_if_not_found;
  config->verbose = old_verbose;
  return err;
}

static struct zsv_ext_command *ext_command_new(const char *id, const char *help,
                                               enum zsv_ext_status (*extmain)(zsv_execution_context ctx, int argc,
                                                                              const char *argv[], struct zsv_opts *)) {
  struct zsv_ext_command *cmd = calloc(1, sizeof(*cmd));
  cmd->id = strdup(id ? id : "");
  cmd->help = help ? strdup(help) : NULL;
  cmd->main = extmain;
  return cmd;
}

static void ext_command_delete(struct zsv_ext_command *cmd) {
  if (cmd) {
    free(cmd->id);
    free(cmd->help);
    free(cmd);
  }
}
