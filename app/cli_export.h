#ifndef ZSV_CLI_EXPORT_H
#define ZSV_CLI_EXPORT_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define ZSV_CLI_EXPORT EMSCRIPTEN_KEEPALIVE
#ifndef NO_PLAYGROUND
#define ZSV_CLI_MAIN main
#else
#define ZSV_CLI_MAIN zsv
#endif
#else
#define ZSV_CLI_EXPORT
#endif

#endif
