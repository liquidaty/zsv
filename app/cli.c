/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/dl.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/string.h>
#include <zsv/utils/dirs.h>
#include <zsv/utils/signal.h>
#include <zsv.h>
#include <zsv/ext.h>
#include "cli_internal.h"
#include "cli_const.h"
#include "cli_export.h"

struct cli_config {
  struct zsv_ext *extensions;
  char err_if_not_found;
  char filepath[FILENAME_MAX];
  char verbose;
};

static struct zsv_ext *zsv_ext_new(const char *dl_name, const char *id, char verbose);

#include "cli_ini.c"

typedef int (cmd_main)(int argc, const char *argv[]);
typedef int (zsv_cmd)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used);
typedef int (*cmd_reserved)();

struct builtin_cmd {
  const char *name;
  cmd_main *main;
  zsv_cmd *cmd;
};

#include "zsv_main.h"

#define CLI_BUILTIN_DECL(x) int main_ ## x(int argc, const char *argv[])

#define CLI_BUILTIN_DECL_STATIC(x) static int main_ ## x(int argc, const char *argv[])

CLI_BUILTIN_DECL_STATIC(license);
CLI_BUILTIN_DECL_STATIC(thirdparty);
CLI_BUILTIN_DECL_STATIC(help);
CLI_BUILTIN_DECL_STATIC(version);
CLI_BUILTIN_DECL_STATIC(register);
CLI_BUILTIN_DECL_STATIC(unregister);

ZSV_MAIN_DECL(select);
ZSV_MAIN_DECL(count);
ZSV_MAIN_DECL(paste);
ZSV_MAIN_DECL(2json);
ZSV_MAIN_DECL(2tsv);
ZSV_MAIN_DECL(serialize);
ZSV_MAIN_DECL(flatten);
ZSV_MAIN_DECL(pretty);
ZSV_MAIN_DECL(stack);
ZSV_MAIN_DECL(desc);
ZSV_MAIN_DECL(sql);
ZSV_MAIN_DECL(2db);
ZSV_MAIN_DECL(compare);
ZSV_MAIN_DECL(echo);
ZSV_MAIN_NO_OPTIONS_DECL(prop);
ZSV_MAIN_NO_OPTIONS_DECL(rm);
ZSV_MAIN_NO_OPTIONS_DECL(mv);

#ifdef USE_JQ
ZSV_MAIN_NO_OPTIONS_DECL(jq);
#endif

#define CLI_BUILTIN_CMD(x) { .name = #x, .main = main_ ## x, .cmd = NULL }
#define CLI_BUILTIN_COMMAND(x) { .name = #x, .main = NULL, .cmd = ZSV_MAIN_FUNC(x) }
#define CLI_BUILTIN_NO_OPTIONS_COMMAND(x) { .name = #x, .main = ZSV_MAIN_NO_OPTIONS_FUNC(x), .cmd = NULL }
struct builtin_cmd builtin_cmds[] = {
  CLI_BUILTIN_CMD(license),
  CLI_BUILTIN_CMD(thirdparty),
  CLI_BUILTIN_CMD(help),
  CLI_BUILTIN_CMD(version),
  CLI_BUILTIN_CMD(register),
  CLI_BUILTIN_CMD(unregister),

  CLI_BUILTIN_COMMAND(select),
  CLI_BUILTIN_COMMAND(count),
  CLI_BUILTIN_COMMAND(paste),
  CLI_BUILTIN_COMMAND(2json),
  CLI_BUILTIN_COMMAND(2tsv),
  CLI_BUILTIN_COMMAND(serialize),
  CLI_BUILTIN_COMMAND(flatten),
  CLI_BUILTIN_COMMAND(pretty),
  CLI_BUILTIN_COMMAND(stack),
  CLI_BUILTIN_COMMAND(desc),
  CLI_BUILTIN_COMMAND(sql),
  CLI_BUILTIN_COMMAND(2db),
  CLI_BUILTIN_COMMAND(compare),
  CLI_BUILTIN_COMMAND(echo),
  CLI_BUILTIN_NO_OPTIONS_COMMAND(prop),
  CLI_BUILTIN_NO_OPTIONS_COMMAND(rm),
  CLI_BUILTIN_NO_OPTIONS_COMMAND(mv)
#ifdef USE_JQ
  , CLI_BUILTIN_NO_OPTIONS_COMMAND(jq)
#endif
};

struct zsv_execution_data {
  struct zsv_ext *ext;
  zsv_parser parser;

  int argc;
  const char **argv;

  void *custom_context; // user-defined
};

static struct zsv_opts ext_parser_opts(zsv_execution_context ctx) {
  (void)(ctx);
  return zsv_get_default_opts();
}

char **custom_cmd_names = NULL;

static enum zsv_ext_status ext_init(struct zsv_ext *ext);

static int config_init(struct cli_config *c, char err_if_dl_not_found, char do_init, char verbose) {
  memset(c, 0, sizeof(*c));
  size_t len = get_ini_file(c->filepath, FILENAME_MAX);
  if(!len) {
    fprintf(stderr, "Unable to get config filepath!\n");
    return 1;
  }

  int rc = parse_extensions_ini(c, err_if_dl_not_found, verbose);
  if(rc == 0 && do_init) {
    for(struct zsv_ext *ext = c->extensions; ext; ext = ext->next) {
      if(ext_init(ext) != zsv_ext_status_ok)
        fprintf(stderr, "Error: unable to initialize extension %s\n", ext->id);
    }
  }
  return rc;
}

static int config_free(struct cli_config *c) {
  return zsv_unload_custom_cmds(c->extensions);
}

static void ext_set_context(zsv_execution_context ctx, void *custom_context) {
  struct zsv_execution_data *d = ctx;
  d->custom_context = custom_context;
}

static void *ext_get_context(zsv_execution_context ctx) {
  struct zsv_execution_data *d = ctx;
  return d->custom_context;
}

static void ext_set_parser(zsv_execution_context ctx, zsv_parser parser) {
  struct zsv_execution_data *d = ctx;
  d->parser = parser;
}

static zsv_parser ext_get_parser(zsv_execution_context ctx) {
  struct zsv_execution_data *d = ctx;
  return d->parser;
}

static void execution_context_free(struct zsv_execution_data *d) {
  (void)(d);
}

static enum zsv_ext_status execution_context_init(struct zsv_execution_data *d,
                                                  int argc, const char *argv[]) {
  memset(d, 0, sizeof(*d));
  d->argv = argv;
  d->argc = argc;
  return zsv_ext_status_ok;
}

static char *zsv_ext_errmsg(enum zsv_ext_status stat, zsv_execution_context ctx) {
  switch(stat) {
  case zsv_ext_status_ok:
    return strdup("No error");
  case zsv_ext_status_memory:
    return strdup("Out of memory");
  case zsv_ext_status_unrecognized_cmd:
    if(!(ctx && ((struct zsv_execution_data *)ctx)->argc > 0))
      return strdup("Unrecognized command");
    else {
      char *s;
      struct zsv_execution_data *d = ctx;
      asprintf(&s, "Unrecognized command %s", d->argv[1]);
      return s;
    }
    /* use zsv_ext_status_other for silent errors. will not attempt to call errcode() or errstr() */
  case zsv_ext_status_other:
    /* use zsv_ext_status_err for custom errors. will attempt to call errcode() and errstr()
       for custom error code and message (if not errcode or errstr not provided, will be silent) */
  case zsv_ext_status_error:
    return NULL;
  }
  return NULL;
}

/* handle_ext_err(): return 1 if handled via ext callbacks, 0 otherwise */
static int handle_ext_err(struct zsv_ext *ext, zsv_execution_context ctx,
                          enum zsv_ext_status stat) {
  int rc = stat;
  char *msg = zsv_ext_errmsg(stat, ctx);
  if(msg) {
    fprintf(stderr, "Error in extension %s: %s\n", ext->id, msg);
    free(msg);
  } else if(ext->module.errcode) {
    int ext_err = rc = ext->module.errcode(ctx);
    char *errstr = ext->module.errstr ? ext->module.errstr(ctx, ext_err) : NULL;
    if(errstr) {
      fprintf(stderr, "Error (%s): %s\n", ext->id, errstr);
      if(ext->module.errfree)
        ext->module.errfree(errstr);
    }
  } else
    fprintf(stderr, "An unknown error occurred in extension %s\n", ext->id);
  return rc;
}

static void ext_set_help(zsv_execution_context ctx, const char *help) {
  struct zsv_execution_data *data = ctx;
  if(data && data->ext) {
    free(data->ext->help);
    data->ext->help = help ? strdup(help) : NULL;
  }
}

static void ext_set_license(zsv_execution_context ctx, const char *license) {
  struct zsv_execution_data *data = ctx;
  if(data && data->ext) {
    free(data->ext->license);
    data->ext->license = license ? strdup(license) : NULL;
  }
}

static char *dup_str_array(const char *ss[]) {
  size_t len = 0;
  for(int i = 0; ss && ss[i]; i++)
    len += strlen(ss[i]);

  if(!len) return NULL;
  char *mem = malloc(len + 2 * sizeof(*mem));
  if(mem) {
    char *tmp = mem;
    for(int i = 0; ss && ss[i]; i++) {
      size_t n = strlen(ss[i]);
      if(n) {
        memcpy(tmp, ss[i], n);
        tmp += n;
      }
    }
    mem[len] = mem[len+1] = '\0';
  }
  return mem;
}

static void ext_set_thirdparty(zsv_execution_context ctx, const char *thirdparty[]) {
  struct zsv_execution_data *data = ctx;
  if(data && data->ext) {
    free(data->ext->thirdparty);
    data->ext->thirdparty = dup_str_array(thirdparty);
  }
}

static enum zsv_ext_status ext_add_command(zsv_execution_context ctx,
                                           const char *id, const char *help,
                                           zsv_ext_main extmain) {
  struct zsv_execution_data *data = ctx;
  if(data && data->ext && data->ext->commands_next
     && id && *id && extmain) {
    struct zsv_ext_command *cmd = ext_command_new(id, help, extmain);
    if(cmd) {
      *data->ext->commands_next = cmd;
      data->ext->commands_next = &cmd->next;
      return zsv_ext_status_ok;
    }
  }
  return zsv_ext_status_error;
}

static enum zsv_ext_status ext_parse_all(zsv_execution_context ctx,
                                         void *user_context,
                                         void (*row_handler)(void *ctx),
                                         struct zsv_opts *const custom,
                                         struct zsv_prop_handler *custom_prop
                                         ) {
  struct zsv_opts opts = custom ? *custom : ext_parser_opts(ctx);
  struct zsv_prop_handler custom_prop_handler = custom_prop ? *custom_prop : zsv_get_default_custom_prop_handler();

  if(row_handler)
    opts.row_handler = row_handler;
  zsv_parser parser = zsv_new(&opts);
  if(!parser)
    return zsv_ext_status_memory;

  ext_set_parser(ctx, parser);
  ext_set_context(ctx, user_context);
  zsv_set_context(parser, ctx);

  zsv_handle_ctrl_c_signal();
  enum zsv_status stat = zsv_status_ok;
  while(!zsv_signal_interrupted
        && (stat = zsv_parse_more(parser)) == zsv_status_ok) ;
  if(stat == zsv_status_no_more_input
     || (zsv_signal_interrupted && stat == zsv_status_ok))
    stat = zsv_finish(parser);
  zsv_delete(parser);

  return stat ? zsv_ext_status_error : zsv_ext_status_ok;
}

static struct zsv_ext_callbacks *zsv_ext_callbacks_init(struct zsv_ext_callbacks *e) {
  if(e) {
    memset(e, 0, sizeof(*e));
    e->set_row_handler = zsv_set_row_handler;
    e->set_context = zsv_set_context;
    e->parse_more = zsv_parse_more;
    e->abort = zsv_abort;
    e->cell_count = zsv_cell_count;
    e->get_cell = zsv_get_cell;
    e->finish = zsv_finish;
    e->delete = zsv_delete;

    e->ext_set_context = ext_set_context;
    e->ext_get_context = ext_get_context;
    e->ext_get_parser = ext_get_parser;
    e->ext_add_command = ext_add_command;

    e->ext_set_help = ext_set_help;
    e->ext_set_license = ext_set_license;
    e->ext_set_thirdparty = ext_set_thirdparty;

    e->ext_parse_all = ext_parse_all;
    e->ext_parser_opts = ext_parser_opts;
  }
  return e;
}

static enum zsv_ext_status ext_init(struct zsv_ext *ext) {
  enum zsv_ext_status stat = zsv_ext_status_ok;
  if(!ext->inited) {
    if(!ext->ok)
      return zsv_ext_status_error;

    ext->inited = zsv_init_started;
    struct zsv_ext_callbacks cb;
    zsv_ext_callbacks_init(&cb);
    ext->commands_next = &ext->commands;

    struct zsv_execution_data d;
    memset(&d, 0, sizeof(d));
    d.ext = ext;
    if((stat = ext->module.init(&cb, &d)) != zsv_ext_status_ok) {
      handle_ext_err(ext, NULL, stat);
      return stat;
    }
    ext->inited = zsv_init_ok; // init ok
  }
  return stat;
}

static int zsv_unload_custom_cmds(struct zsv_ext *ext) {
  int err = 0;
  for(struct zsv_ext *next; ext; ext = next) {
    next = ext->next;
    if(zsv_ext_delete(ext))
      err = 1;
  }
  return err;
}

struct zsv_ext_command *find_ext_cmd(struct zsv_ext *ext, const char *id) {
  for(struct zsv_ext_command *cmd = ext->commands; cmd; cmd = cmd->next)
    if(!strcmp(cmd->id, id))
      return cmd;
  return NULL;
}

static enum zsv_ext_status run_extension(int argc, const char *argv[], struct zsv_ext *ext) {
  enum zsv_ext_status stat = zsv_ext_status_error;
  if(ext) {
    if((stat = ext_init(ext)) != zsv_ext_status_ok)
      return stat;

    struct zsv_ext_command *cmd = find_ext_cmd(ext, argv[1] + 3);
    if(!cmd) {
      fprintf(stderr, "Unrecognized command for extension %s: %s\n",
              ext->id, argv[1] + 3);
      return zsv_ext_status_unrecognized_cmd;
    }

    struct zsv_execution_data ctx;

    if((stat = execution_context_init(&ctx, argc, argv)) == zsv_ext_status_ok) {
      struct zsv_opts opts;
      zsv_args_to_opts(argc, argv, &argc, argv, &opts, NULL);
      zsv_set_default_opts(opts);
      // need a corresponding zsv_set_default_custom_prop_handler?
      stat = cmd->main(&ctx, ctx.argc - 1, &ctx.argv[1]);
    }

    if(stat != zsv_ext_status_ok)
      stat = handle_ext_err(ext, &ctx, stat);
    execution_context_free(&ctx);
  }
  return stat;
}

/* havearg(): case-insensitive partial arg matching */
char havearg(const char *arg,
             const char *form1, size_t min_len1,
             const char *form2, size_t min_len2) {
  size_t len = strlen(arg);
  if(!min_len1)
    min_len1 = strlen(form1);
  if(len > min_len1)
    min_len1 = len;

  if(!zsv_strincmp_ascii((const unsigned char *)arg, min_len1,
			 (const unsigned char *)form1, min_len1))
    return 1;

  if(form2) {
    if(!min_len2)
      min_len2 = strlen(form2);
    if(len > min_len2)
      min_len2 = len;
    if(!zsv_strincmp_ascii((const unsigned char *)arg, min_len2,
			   (const unsigned char *)form2, min_len2))
      return 1;
  }
  return 0;
}

static struct builtin_cmd *find_builtin(const char *cmd_name) {
  int builtin_cmd_count = sizeof(builtin_cmds)/sizeof(*builtin_cmds);
  for(int i = 0; i < builtin_cmd_count; i++)
    if(havearg(cmd_name, builtin_cmds[i].name, 0, 0, 0))
      return &builtin_cmds[i];
  return NULL;
}

#include "builtin/license.c"
#include "builtin/thirdparty.c"
#include "builtin/help.c"
#include "builtin/version.c"
#include "builtin/register.c"

static const char *extension_cmd_from_arg(const char *arg) {
  if(strlen(arg) > 3 && arg[2] == '-')
    return arg + 3;
  return NULL;
}

#ifndef ZSV_CLI_MAIN
#define ZSV_CLI_MAIN main
#endif

ZSV_CLI_EXPORT
int ZSV_CLI_MAIN(int argc, const char *argv[]) {
  const char **alt_argv = NULL;
  struct builtin_cmd *builtin = find_builtin(argc > 1 ? argv[1] : "help");
  if(builtin) {
    /* help is different from other commands: zsv help <arg> is treated as
       if it was zsv <arg> --help
    */
    if(builtin->main == main_help && argc > 2) {
      struct builtin_cmd *help_builtin = find_builtin(argv[2]);
      if(help_builtin) {
        const char *argv_tmp[2] = {
          argv[2],
          "--help"
        };
        if(help_builtin->main)
          return help_builtin->main(2, argv_tmp);
        else if(help_builtin->cmd) {
          char opts_used[ZSV_OPTS_SIZE_MAX] = { 0 };
          struct zsv_opts opts = { 0 };
          return help_builtin->cmd(2, argv_tmp, &opts, NULL, opts_used);
        } else
          return fprintf(stderr, "Unexpected syntax!\n");
      } else {
        const char *ext_cmd = extension_cmd_from_arg(argv[2]);
        if(ext_cmd) {
          alt_argv = calloc(3, sizeof(*alt_argv));
          alt_argv[0] = argv[0];
          alt_argv[1] = argv[2];
          alt_argv[2] = "--help";
          argc = 3;
          argv = alt_argv;
        } else {
          fprintf(stderr, "Unrecognized command %s\n", argv[2]);
          free(alt_argv);
          return 1;
        }
      }
    } else {
      if(builtin->main)
        return builtin->main(argc - 1, argc > 1 ? &argv[1] : NULL);

      char opts_used[ZSV_OPTS_SIZE_MAX];
      struct zsv_opts opts;
      enum zsv_status stat = zsv_args_to_opts(argc, argv, &argc, argv, &opts, opts_used);
      if(stat == zsv_status_ok)
        return builtin->cmd(argc - 1, argc > 1 ? &argv[1] : NULL, &opts, NULL, opts_used);
      return stat;
    }
  }

  int err = 1;
  if(strlen(argv[1]) > 3 && argv[1][2] == '-') { // this is an extension command
    struct cli_config config;
    memset(&config, 0, sizeof(config));
    if(!(err = add_extension(argv[1], &config.extensions, 0, 0)))
      err = run_extension(argc, argv, config.extensions);
    if(config_free(&config) && !err)
      err = 1;
  } else
    fprintf(stderr, "Unrecognized command %s\n", argv[1]), err = 1;

  free(alt_argv);
  return err;
}

// extensions
static enum zsv_ext_status zsv_ext_delete(struct zsv_ext *ext) {
  enum zsv_ext_status stat = zsv_ext_status_ok;
  if(ext) {
    if(ext->dl) {
      if(ext->module.exit && ext->inited) {
        stat = ext->module.exit();
        if(stat != zsv_ext_status_ok)
          handle_ext_err(ext, NULL, stat);
      }
      dlclose(ext->dl);
    }
    for(struct zsv_ext_command *next, *cmd = ext->commands; cmd; cmd = next) {
      next = cmd->next;
      ext_command_delete(cmd);
    }
    free(ext->id);
    free(ext->help);
    free(ext->license);
    free(ext->thirdparty);
    free(ext);
  }
  return stat;
}

/**
 * zsv_ext_init(): return error
 */
static int zsv_ext_init(void *dl, const char *dl_name, struct zsv_ext *ext) {
  memset(ext, 0, sizeof(*ext));
  if(dl) {
#define zsv_ext_func_assign(sig, x) ext->module.x = sig zsv_dlsym(dl, "zsv_ext_" #x)
#include "cli_internal.c.in"

    /* check if required functions are present */
    if(ext->module.id)
      return 0;
    fprintf(stderr, "Dynamic library %s missing required function zsv_ext_id()\n", dl_name);
  }
  return 1;
}

#ifdef __APPLE__
#include <dlfcn.h>
#endif

#ifdef _WIN32
char *get_module_name(HMODULE handle) {
  wchar_t *pth16 = (wchar_t*)malloc(32768); // max long path length
  DWORD n16 = GetModuleFileNameW(handle,pth16,32768);
  if (n16 <= 0) {
    free(pth16);
    return NULL;
  }
  pth16[n16] = L'\0';
  DWORD n8 = WideCharToMultiByte(CP_UTF8, 0, pth16, -1, NULL, 0, NULL, NULL);
  if (n8 == 0) {
    free(pth16);
    return NULL;
  }
  char *filepath = (char*)malloc(++n8);
  if (!WideCharToMultiByte(CP_UTF8, 0, pth16, -1, filepath, n8, NULL, NULL)) {
    free(pth16);
    free(filepath);
    return NULL;
  }
  free(pth16);
  return filepath;
}
#endif

static char *dl_name_from_func(const void *func) {
#ifdef _WIN32
  HMODULE hModule = NULL;
  if(GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCTSTR)func, &hModule))
    return get_module_name(hModule);
#endif

#ifdef __APPLE__
  Dl_info info;
  if(dladdr(func, &info))
    return strdup(info.dli_fname);
#endif
  return NULL;
}


#ifdef __EMSCRIPTEN__
static void* zsv_dlopen(const char *path, int mode) {
  fprintf(stderr, "Emscripten dlopen() does not work for shared libs > 4kb!"
          "fix this when it does."
          "See https://github.com/emscripten-core/emscripten/issues/15795\n");
  return dlopen(path, mode);
}
#else
#define zsv_dlopen dlopen
#endif

static struct zsv_ext *zsv_ext_new(const char *dl_name, const char *id, char verbose) {
  struct zsv_ext *dl = NULL;
  struct zsv_ext tmp;
#ifdef __EMSCRIPTEN__
  void *h = NULL;
#else
  void *h = zsv_dlopen(dl_name, RTLD_LAZY);
#endif
  if(!h) {
    /* not in system search path. try again in the same path as our executable (or in /ext if in emcc) */
    char exe_path[FILENAME_MAX];
    char have_path = 0;
#ifdef __EMSCRIPTEN__
    size_t n = snprintf(exe_path, sizeof(exe_path), "/tmp/zsvext%s.so", id);
    if(n > 0 && n < sizeof(exe_path)) {
      fprintf(stderr, "Opening %s\n", exe_path);
      h = zsv_dlopen(exe_path, RTLD_LAZY);
    } else
      fprintf(stderr, "Opening whaa???\n");
#else
    size_t n = zsv_get_executable_path(exe_path, sizeof(exe_path));
    if(n > 0 && n < sizeof(exe_path)) {
      char *end = strrchr(exe_path, FILESLASH);
      if(end) {
        end[1] = '\0';
        n = strlen(exe_path);
        size_t n2 = snprintf(exe_path + n, sizeof(exe_path) - n, "%s", dl_name);
        if(n2 > 0 && n + n2 < sizeof(exe_path)) {
          have_path = 1;
          h = zsv_dlopen(exe_path, RTLD_LAZY);
        }
      }
    }
#endif
    if(verbose) {
      if(have_path)
        fprintf(stderr, "Library %s not found in path; trying exe dir %s\n",
                dl_name, exe_path);
      else
        fprintf(stderr, "Library %s not found in path; cannot determine exe dir\n",
                dl_name);
    }
  }
  if(!h)
    fprintf(stderr, "Library %s not found\n", dl_name);

  /* run zsv_ext_init to add to our extension list, even if it's invalid */
  tmp.ok = !zsv_ext_init(h, dl_name, &tmp);
  const char *m_id = tmp.ok && tmp.module.id ? tmp.module.id() : NULL;
  if(h && (!m_id || strcmp(m_id, (const char *)id))) {
    fprintf(stderr, "Library %s: unexpected result from zsv_ext_id()\n"
            "(got %s, expected %s)\n",
            id, m_id ? m_id : "(null)", id);
    tmp.ok = 0;
  } else if(h && tmp.ok) {
    if(verbose) {
      char *image_name = dl_name_from_func((const void *)tmp.module.id);
      fprintf(stderr, "Loaded %s from %s\n", dl_name, image_name ? image_name : "unknown location");
      free(image_name);
    }
  }
  tmp.dl = h;
  tmp.id = strdup((const char *)id);
  if(!(dl = calloc(1, sizeof(*dl))))
    fprintf(stderr, "Out of memory\n");
  else
    *dl = tmp;
  return dl;
}
