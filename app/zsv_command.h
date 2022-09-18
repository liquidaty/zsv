#ifndef ZSV_COMMAND_H
#define ZSV_COMMAND_H

/**
 * Include zsv headers and entry point declaration
 * If the command will *not* use common zsv parsing options, then prior to
 * including this file, define
 *   ZSV_COMMAND_NO_OPTIONS
 */

#include <zsv.h>
#include <zsv/utils/arg.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/compiler.h>
#include <zsv/utils/signal.h>
#include <zsv/utils/memmem.h>

#include "zsv_main.h"

#define APPNAME1(x) #x
#define APPNAME2(x) APPNAME1(x)
#define APPNAME APPNAME2(ZSV_COMMAND)

#include <zsv/utils/err.h>

#ifdef ZSV_COMMAND_NO_OPTIONS
ZSV_MAIN_NO_OPTIONS_DECL(ZSV_COMMAND);
#else
ZSV_MAIN_DECL(ZSV_COMMAND);
#endif

/**
 * If we are currently compiling a standalone executable (e.g. zsv_select),
 * then include the main() code
 */
#ifndef ZSV_CLI
#include "zsv_command_standalone.c"
#endif

#endif
