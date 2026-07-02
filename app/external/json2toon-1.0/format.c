/* json2toon - the status-code stringifier shared by both directions. */
#include "internal.h"

const char *j2t_strerror(int rc, const char *parse_msg) {
  if (rc == JSON2TOON_ERR_PARSE && parse_msg)
    return parse_msg;
  switch (rc) {
#define JSON2TOON_MSG_ENTRY(name, value, msg) case name: return msg;
    JSON2TOON_ERROR_LIST(JSON2TOON_MSG_ENTRY)
#undef JSON2TOON_MSG_ENTRY
    default: return "unknown error";
  }
}
