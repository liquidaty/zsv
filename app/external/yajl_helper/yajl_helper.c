#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>

static inline void *yh_memdup(const void *src, size_t n) {
  void *m = calloc(1, n + 2);
  if(n)
    memcpy(m, src, n);
  return m;
}

#include "yajl_helper.h"
#include "yajl_helper/json_value.h"

#define YAJL_HELPER_LEVEL(st) (st->level - st->level_offset)

unsigned int yajl_helper_level(struct yajl_helper_parse_state *st) {
  return YAJL_HELPER_LEVEL(st);
}

void yajl_helper_set_data(struct yajl_helper_parse_state *st, void *data) {
  st->data = data;
}

void *yajl_helper_data(struct yajl_helper_parse_state *st) {
  return st->data;
}

/**
 * Print the current path for e.g. error reporting
 */
void yajl_helper_dump_path(struct yajl_helper_parse_state *st, FILE *out) {
  for(unsigned int i = 0; i < st->level; i++) {
    switch(st->stack[i]) {
    case '[':
      fwrite(&st->stack[i],1,1,out);
      break;
    case '{':
      fwrite(&st->stack[i],1,1,out);
      if(st->map_keys[i])
        fwrite(st->map_keys[i], 1, strlen(st->map_keys[i]), out);
      break;
    }
  }
}

// yajl_helper_level_offset(): return error
int yajl_helper_level_offset(struct yajl_helper_parse_state *st, unsigned int offset) {
  if(offset > st->level) {
    fprintf(stderr, "yajl_helper_level_offset: level_offset may not exceed level\n");
    return 1;
  }

  st->level_offset = offset;
  return 0;
}

unsigned int yajl_helper_array_index_plus_1(struct yajl_helper_parse_state *st, unsigned int offset) {
  if(offset > YAJL_HELPER_LEVEL(st))
    return 0;
  unsigned int level = st->level - offset;
  if(level > 0 && st->stack[level-1] == '[' && level <= st->max_level)
    return st->item_ind[level-1] + 1;
  return 0;
}

unsigned int yajl_helper_element_index_plus_1(struct yajl_helper_parse_state *st, unsigned int offset) {
  if(offset > YAJL_HELPER_LEVEL(st))
    return 0;
  unsigned int level = st->level - offset;
  if(level > 0 && strchr("{[", st->stack[level-1]) && level <= st->max_level)
    return st->item_ind[level-1] + 1;
  return 0;
}


long long json_value_long(struct json_value *value, int *err) {
  if(!value)
    *err = 1;
  else {
    switch(value->type) {
    case json_value_bool:
      return value->val.i ? 1 : 0;
    case json_value_int:
      return value->val.i;
    case json_value_double:
      return (long long)value->val.dbl;
    case json_value_number_string:
      if(value->strlen && value->strlen + 1 < 128) {
        char buff[128];
        memcpy(buff, value->val.s, value->strlen);
        buff[value->strlen-1] = '\0';
        errno = 0;
        char *end;
        long l = strtol(buff, &end, 10);
        if(*end) {
          if(err)
            *err = 1;
        } else
          return (long long)l;
      }
      break;
    default:
      break;
    }
  }
  return 0;
}

double json_value_dbl(struct json_value *value, int *err) {
  if(!value)
    *err = 1;
  else {
    switch(value->type) {
    case json_value_bool:
      return (double)value->val.i ? 1 : 0;
    case json_value_int:
      return (double)value->val.i;
    case json_value_double:
      return value->val.dbl;
    case json_value_number_string:
      if(value->strlen && value->strlen + 1 < 128) {
        char buff[128];
        memcpy(buff, value->val.s, value->strlen);
        buff[value->strlen-1] = '\0';
        errno = 0;
        double d = strtod(buff, NULL);
        if ((d == HUGE_VAL || d == -HUGE_VAL) && errno == ERANGE) // overflow
          *err = 1;
        else
          return d;
      }
      break;
    default:
      break;
    }
  }
  return 0;
}

char json_value_truthy(struct json_value *value) {
  switch(value->type) {
  case json_value_null:
    return 0;
  case json_value_bool:
  case json_value_int:
    return value->val.i ? 1 : 0;
  case json_value_double:
    return value->val.dbl ? 1 : 0;
  case json_value_string:
  case json_value_number_string:
    if(value->strlen && value->strlen + 1 < 128) {
      char buff[128];
      memcpy(buff, value->val.s, value->strlen);
      buff[value->strlen-1] = '\0';
      errno = 0;
      double d = strtod(buff, NULL);
      if ((d == HUGE_VAL || d == -HUGE_VAL) && errno == ERANGE) // overflow
        return 0;
      return d ? 1 : 0;
    }
    break;
  case json_value_error:
    return 0;
  }
  return 0;
}

/**
 * Print any error from the yajl parser
 * Returns non-zero
 */
int yajl_helper_print_err(yajl_handle yajl,
                          unsigned char *last_parsed_buff,
                          size_t last_parsed_buff_len
                          ) {
  unsigned char *str = yajl_get_error(yajl, 1,
                                      last_parsed_buff, last_parsed_buff_len);
  if(str) {
    fprintf(stderr, "Error parsing JSON: %s", (const char *)str);
    yajl_free_error(yajl, str);
  }
  return 1;
}


const char *yajl_helper_get_map_key(struct yajl_helper_parse_state *st, unsigned int offset) {
  if(YAJL_HELPER_LEVEL(st) >= offset + 1) {
    unsigned int level = st->level - offset;
    if(level > 0 && st->stack[level-1] == '{' && level <= st->max_level)
      return st->map_keys[level - 1];
  }
  return NULL;
}

static char yajl_helper_got_path_aux(struct yajl_helper_parse_state *st, unsigned int level, const char *path) {
  for(unsigned i = 1; *path && i <= level; path++, i++) {
    unsigned this_level = st->level_offset + i;
    switch(*path) {
    case '{':
    case '[':
      if(st->stack[this_level - 1] != *path)
        return 0;

      if(*path == '{' && path[1]) { // check map key
        const char *map_key = st->map_keys[this_level - 1];
        size_t len = strlen(map_key);
        if(path[1] == '*' && (!path[2] || path[2] == '{' || path[2] == '['))
          path++;
        else {
          if(strncmp(map_key, path + 1, len))
            return 0;
          path += len;
        }
      }
      break;
    default: // map key start
      return 0;
    }
  }
  return 1;
}

char yajl_helper_got_path(struct yajl_helper_parse_state *st, unsigned int level, const char *path) {
  if(YAJL_HELPER_LEVEL(st) != level)
    return 0;
  return yajl_helper_got_path_aux(st, level, path);
}

char yajl_helper_got_path_prefix(struct yajl_helper_parse_state *st, unsigned int level, const char *path) {
  if(YAJL_HELPER_LEVEL(st) < level)
    return 0;
  return yajl_helper_got_path_aux(st, level, path);
}

char yajl_helper_path_is(struct yajl_helper_parse_state *st, const char *path) {
  unsigned int level = 0;
  for(int i = 0; path[i]; i++)
    if(path[i] == '{' || path[i] == '[')
      level++;
  return yajl_helper_got_path(st, level, path);
}

void yajl_helper_parse_state_free(struct yajl_helper_parse_state *st) {
  if(st) {
    for(unsigned int i = 0; i < st->max_level; i++)
      free(st->map_keys[i]);
    free(st->stack);
    free(st->map_keys);
    free(st->item_ind);
    if(st->yajl)
      yajl_free(st->yajl);
    memset(st, 0, sizeof(*st));
  }
}

static int yajl_helper_start_array(void *ctx) {
  struct yajl_helper_parse_state *st = ctx;
  if(st->level < st->max_level) {
    st->stack[st->level] = '[';
    st->item_ind[st->level] = 0;
  }
  st->level++;

  if(st->start_array)
    return st->start_array(st);

  return 1;
}

static int yajl_helper_end_array(void *ctx) {
  struct yajl_helper_parse_state *st = ctx;
  st->level--;
  if(st->level && strchr("[{", st->stack[st->level-1]) && st->level <= st->max_level)
    st->item_ind[st->level-1]++;

  int rc = 1;
  if(st->end_array)
    rc = st->end_array(st);

  if(st->level_offset > st->level) {
    fprintf(stderr, "yajl_helper_end_array: level_offset exceeds level\n");
    rc = 0;
  }
  return rc;
}


static int yajl_helper_start_map(void *ctx) {
  struct yajl_helper_parse_state *st = ctx;
  if(st->level < st->max_level) {
    st->stack[st->level] = '{';
    st->item_ind[st->level] = 0;
  }
  st->level++;

  if(st->start_map)
    return st->start_map(st);

  return 1;
}

static int yajl_helper_end_map(void *ctx) {
  struct yajl_helper_parse_state *st = ctx;
  st->level--;
  if(st->level < st->max_level && st->map_keys[st->level]) {
    free(st->map_keys[st->level]);
    st->map_keys[st->level] = NULL;
  }

  if(st->level && strchr("{[", st->stack[st->level-1]) && st->level <= st->max_level)
    st->item_ind[st->level-1]++;

  int rc = 1;
  if(st->end_map)
    rc = st->end_map(st);

  if(st->level_offset > st->level) {
    fprintf(stderr, "yajl_helper_end_map: level_offset exceeds level\n");
    rc = 0;
  }
  return rc;
}

static int yajl_helper_map_key(void *ctx, const unsigned char *stringVal, size_t len) {
  struct yajl_helper_parse_state *st = ctx;

  if(st->level <= st->max_level) {
    if(st->map_keys[st->level - 1])
      free(st->map_keys[st->level - 1]);
    char *str;
    if((st->map_keys[st->level - 1] = str = malloc(1 + len))) {
      memcpy(str, stringVal, len);
      str[len] = '\0';
    }
  }

  if(st->map_key)
    return st->map_key(st, stringVal, len);

  return 1;
}

static inline int process_value(struct yajl_helper_parse_state *st,
                            void *ctx, struct json_value *v) {
  if(st->value) {
    int rc = st->value(ctx, v);
    if(st->level && strchr("{[", st->stack[st->level-1]) && st->level <= st->max_level)
      st->item_ind[st->level-1]++;
    return rc;
  }
  return 1;
}

static int yajl_helper_number_str(void * ctx, const char * numberVal,
                                  size_t numberLen) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_number_string;
  value.val.s = (unsigned char *)numberVal;
  value.strlen = numberLen;
  return process_value(st, ctx, &value);
}

/* unified calls to a single yajl_helper_value */
static int yajl_helper_string(void *ctx, const unsigned char *stringVal, size_t len) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_string;
  value.val.s = (unsigned char *)stringVal;
  value.strlen = len;
  return process_value(st, ctx, &value);
}

static int yajl_helper_double(void *ctx, double d) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_double;
  value.val.dbl = d;
  return process_value(st, ctx, &value);
}

static int yajl_helper_int(void *ctx, long long integerVal) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_int;
  value.val.i = integerVal;
  return process_value(st, ctx, &value);
}

static int yajl_helper_bool(void *ctx, int boolean) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_bool;
  value.val.i = boolean ? 1 : 0;
  return process_value(st, ctx, &value);
}

static int yajl_helper_null(void *ctx) {
  struct yajl_helper_parse_state *st = ctx;
  struct json_value value;
  value.type = json_value_null;
  return process_value(st, ctx, &value);
}

static int yajl_helper_error(void * ctx, const unsigned char *buf,
                             unsigned bufLen, int err_no) {
  (void)(err_no);
  struct yajl_helper_parse_state *st = ctx;

  if(!st->value)
    return 0; // stop parsing

  struct json_value value;
  value.type = json_value_error;
  value.val.s = (unsigned char *)buf;
  value.strlen = bufLen;
  return process_value(st, ctx, &value);
}

void yajl_helper_callbacks_init(yajl_callbacks *callbacks, char nums_as_strings) {
  yajl_callbacks x = {
    yajl_helper_null,
    yajl_helper_bool,
    yajl_helper_int,
    yajl_helper_double,
    nums_as_strings ? yajl_helper_number_str : NULL,
    yajl_helper_string,
    yajl_helper_start_map,
    yajl_helper_map_key,
    yajl_helper_end_map,
    yajl_helper_start_array,
    yajl_helper_end_array,
    yajl_helper_error
  };
  memcpy(callbacks, &x, sizeof(x));
}

yajl_status yajl_helper_parse_state_init(
                                         struct yajl_helper_parse_state *st,
                                         unsigned int max_level,
                                         int (*start_map)(struct yajl_helper_parse_state *),
                                         int (*end_map)(struct yajl_helper_parse_state *),
                                         int (*map_key)(struct yajl_helper_parse_state *,
                                                        const unsigned char *, size_t),
                                         int (*start_array)(struct yajl_helper_parse_state *),
                                         int (*end_array)(struct yajl_helper_parse_state *),
                                         int (*value)(struct yajl_helper_parse_state *,
                                                      struct json_value *),
                                         void *data
                                         ) {
  memset(st, 0, sizeof(*st));
  st->max_level = max_level ? max_level : 32;

  st->stack = calloc(st->max_level, sizeof(char));
  st->map_keys = calloc(st->max_level, sizeof(char *));
  st->item_ind = calloc(st->max_level, sizeof(*st->item_ind));
  st->yajl = yajl_alloc(&st->callbacks, NULL, st);
  if(!(st->stack && st->map_keys && st->item_ind && st->yajl))
    return yajl_status_error;

  yajl_helper_callbacks_init(&st->callbacks, 0);

  st->start_map = start_map;
  st->end_map = end_map;
  st->map_key = map_key;

  st->start_array = start_array;
  st->end_array = end_array;
  st->value = value;
  st->data = data;
  return yajl_status_ok;
}

///// misc

size_t json_value_default_string(struct json_value *value, const unsigned char **target,
                                 size_t *len) {
  switch(value->type) {
  case json_value_string:
  case json_value_number_string:
    *target = value->val.s;
    *len = value->strlen;
    break;
  case json_value_null:
    *target = NULL;
    *len = 0;
    break;
  default:
    *target = (unsigned char *)"";
    *len = 0;
    break;
  }
  return *len;
}

size_t json_value_to_string_dup(struct json_value *value, char **target, char convert) {
  struct json_value_string jvs;
  json_value_to_string(value, &jvs, convert);

  if(target) {
    char *dupe = jvs.len && jvs.s ? yh_memdup(jvs.s, jvs.len) : NULL;
    if(*target)
      free(*target);
    *target = dupe;
  }
  return jvs.len;
}

/*
  json_value_to_string: return string representation of json value
    will use provided buff if necessary
    returns length of string
    ex:
      struct json_value_string jvs;
      fprintf(stderr, "my value: %.*s\n", json_value_default_string2(v, &jvs), jvs.s);
*/
size_t json_value_to_string(struct json_value *value, struct json_value_string *jvs, char convert_if_not_str) {
  switch(value->type) {
  case json_value_string:
  case json_value_number_string:
    jvs->len = value->strlen;
    jvs->s = value->val.s;
    break;
  default:
    if(!convert_if_not_str) {
      jvs->s = (unsigned char *)"";
      jvs->len = 0;
    } else {
      switch(value->type) {
      case json_value_bool:
        if(value->val.i)
          jvs->s = (unsigned char *)"true";
        else
          jvs->s = (unsigned char *)"false";
        jvs->len = strlen((const char *)jvs->s);
        break;
      case json_value_int:
      case json_value_double:
        {
          int n;
          if(value->type == json_value_int)
            n = snprintf((char *)jvs->_internal, sizeof(jvs->_internal), "%lli", value->val.i);
          else
            n = snprintf((char *)jvs->_internal, sizeof(jvs->_internal), "%lf", value->val.dbl);

          if(n > 0 && (size_t)n < sizeof(jvs->_internal)) {
            jvs->len = n;
            jvs->s = jvs->_internal;
          } else {
            jvs->s = (unsigned char *)"";
            jvs->len = 0;
          }
        }
        break;
      default:
        jvs->s = (unsigned char *)"";
        jvs->len = 0;
        break;
      }
    }
  }
  return jvs->len;
}

unsigned char *json_str_dup_if_len(struct json_value *value) {
  return json_str_dup_if_len_buff(value, NULL, 0);
}

unsigned char *json_str_dup_if_len_buff(struct json_value *value, unsigned char *buff, size_t bufflen) {
  const unsigned char *src;
  size_t len;
  json_value_default_string(value, &src, &len);
  if(len && *src) {
    if(len < bufflen) {
      memcpy(buff, src, len);
      buff[len] = '\0';
      return buff;
    }
    return yh_memdup(src, len);
  }
  return NULL;
}

#ifdef YAJL_HELPER_UTILS
void *linkedlist_reverse(void *p) {
  struct struct_list *sl = (struct struct_list *)p;
  struct struct_list *next1, *next2;

  /*
    a->b->c->d;
    a  b  c->d;
    a<-b  c->d;
    a = b, b = c, c = d;
  */

  if(!sl || !sl->next)
    return sl;
  else
    for(next1 = sl->next, next2 = (sl->next ? sl->next->next : NULL), sl->next = NULL; next2; sl = next1, next1 = next2, next2 = next2->next)
      next1->next = sl;

  next1->next = sl;
  return next1;
}
#endif

///// ----- string_list
char add_string_to_array(struct string_list **head, struct json_value *value) {
  const unsigned char *src;
  size_t len;
  json_value_default_string(value, &src, &len);
  struct string_list *e = malloc(sizeof(*e));
  e->next = *head;
  e->value = src ? yh_memdup(src, len) : NULL;
  *head = e;
  return (e->value ? 1 : 0);
}

void string_list_free(struct string_list *e) {
  struct string_list *n;
  for( ; e; e = n) {
    n = e->next;
    if(e->value)
      free(e->value);
    free(e);
  }
}

/// ---- int_list
void add_int_to_array(struct int_list **head, struct json_value *value) {
  struct int_list *e = malloc(sizeof(*e));
  e->next = *head;
  e->i = (value && value->type == json_value_int ? value->val.i : -1);
  *head = e;
}

void int_list_free(struct int_list *e) {
  struct int_list *n;
  for( ; e; e = n) {
    n = e->next;
    free(e);
  }
}
