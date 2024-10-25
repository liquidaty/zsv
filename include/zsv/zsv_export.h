#ifndef ZSV_EXPORT_H
#define ZSV_EXPORT_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#ifndef ZSV_EXPORT
#define ZSV_EXPORT EMSCRIPTEN_KEEPALIVE
#endif
#else
#ifndef ZSV_EXPORT
#define ZSV_EXPORT
#endif
#endif

#endif
