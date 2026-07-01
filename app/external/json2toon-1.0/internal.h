/* json2toon - internal shared declarations. Not part of the public API. */
#ifndef JSON2TOON_INTERNAL_H
#define JSON2TOON_INTERNAL_H

#include "j2t_config.h"      /* first: feature-test macros */

#include <stddef.h>
#include <stdint.h>
#include "json2toon.h"

/* ---------------------------------------------------------------- errors */

/* Shared status-code stringifier generated from JSON2TOON_ERROR_LIST. Returns
 * `parse_msg` for JSON2TOON_ERR_PARSE when it is non-NULL (so each direction
 * supplies its own wording); otherwise the code's default message. */
const char *j2t_strerror(int rc, const char *parse_msg);

#endif /* JSON2TOON_INTERNAL_H */
