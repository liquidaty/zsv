#ifndef ZSV_CLI_EXPORT_H
#define ZSV_CLI_EXPORT_H

#if defined(__EMSCRIPTEN__)
# include <emscripten.h>
# define ZSV_CLI_EXPORT EMSCRIPTEN_KEEPALIVE
# define ZSV_CLI_MAIN zsv
#else
# define ZSV_CLI_EXPORT
#endif

#endif
