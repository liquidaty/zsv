#ifndef YAJL_HELPER_H
#define YAJL_HELPER_H
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include <stdio.h>

#include "yajl_helper/json_value.h"

typedef struct yajl_helper_parse_state *yajl_helper_t;

enum yajl_helper_option {
  yajl_helper_option_use_number_strings = 1
} ;

struct json_value_string {
  size_t len;
  unsigned char *s; // holds
  unsigned char _internal[128]; // will hold stringified int / float
};

/* experimental:
// to use less memory, json_value_string_unconvertible can be used in lieu of struct json_value_string in json_value_to_string() and json_value_to_string_dup(),
// provided that convert_if_not_str IS ALWAYS ZERO
struct json_value_string_unconvertible {
  size_t len;
  unsigned char *s; // holds}
;
*/
// json_value_to_string(): get stringified json value. if convert_if_not_str = true, will also convert numbers. otherwise, returns 0 if value is not string type
size_t json_value_to_string(struct json_value *value, struct json_value_string *jvs, char convert_if_not_str);

// json_value_to_string_dup(): same as json_value_to_string, but replace the target with a dupe (must be free()d), or NULL if not string type and convert_if_not_str not used
size_t json_value_to_string_dup(struct json_value *value, char **target, char convert_if_not_str);

// json_value_dbl(): return double value; set *err if value is not numeric or can't be converted to dbl
double json_value_dbl(struct json_value *value, int *err);

long long json_value_long(struct json_value *value, int *err);

char json_value_truthy(struct json_value *value);

#ifndef STRUCT_LIST
#define STRUCT_LIST
struct struct_list {
  struct struct_list *next;
};
#endif

void *linkedlist_reverse(void *p);

#ifndef LINKEDLIST_REVERSE
#define LINKEDLIST_REVERSE(struct_name,pp,next_name) do {   \
    struct struct_name *current = *pp;                      \
    struct struct_name *prev = NULL;                        \
    struct struct_name *next;                               \
    while (current != NULL) {                               \
      next = current->next_name;                            \
      current->next_name = prev;                            \
      prev = current;                                       \
      current = next;                                       \
    }                                                       \
    *pp = prev;                                             \
  } while(0)
#endif



yajl_helper_t yajl_helper_new(
                              unsigned int max_level,
                              int (*start_map)(yajl_helper_t),
                              int (*end_map)(yajl_helper_t),
                              int (*map_key)(yajl_helper_t,
                                             const unsigned char *, size_t),
                              int (*start_array)(yajl_helper_t),
                              int (*end_array)(yajl_helper_t),
                              int (*value)(yajl_helper_t,
                                           struct json_value *),
                              void *ctx // caller-provided pointer that is available to callbacks via yajl_helper_ctx()
                              );
void yajl_helper_delete(yajl_helper_t yh);

void yajl_helper_set_ctx(yajl_helper_t yh, void *ctx);
void *yajl_helper_ctx(yajl_helper_t yh);

// get the yajl handle
yajl_handle yajl_helper_yajl(yajl_helper_t yh);

unsigned int yajl_helper_level(yajl_helper_t yh);

// return '[' or '{' to indicate the object type at the given level
char yajl_helper_stack_at(yajl_helper_t st, unsigned level);

// return map key at given level
const char *yajl_helper_map_key_at(yajl_helper_t st, unsigned int level);

// 
unsigned int yajl_helper_item_ind_plus_1_at(yajl_helper_t yh, unsigned int level);

// get the raw level, without any offset
unsigned int yajl_helper_level_raw(yajl_helper_t yh);

// yajl_helper_set_level_offset(): return error
int yajl_helper_set_level_offset(yajl_helper_t yh, unsigned int offset);

// retrieve a previously-set offset
unsigned int yajl_helper_level_offset(yajl_helper_t yh);

unsigned int yajl_helper_array_index_plus_1(yajl_helper_t, unsigned int offset);
unsigned int yajl_helper_element_index_plus_1(yajl_helper_t yh, unsigned int offset);

/* json_str_dup_if_len: return a dupe of the string value, or null if none */
unsigned char *json_str_dup_if_len(struct json_value *value);

// json_str_dup_if_len_buff(): copy value into buff, if sufficient bufflen, else
// malloc() a new string and return that. returns buff, if buff was written to, else new malloc'd mem
unsigned char *json_str_dup_if_len_buff(struct json_value *value, unsigned char *buff, size_t bufflen);

/*
 * yajl_helper_got_path() and yajl_helper_got_path_prefix() are the same except that the former
 * requires that the current level is equal to the level argument, and the latter only requires
 * that the current level is greater than or equal to the level argument
 */
char yajl_helper_got_path(yajl_helper_t yh, unsigned int level, const char *path);
char yajl_helper_got_path_prefix(yajl_helper_t yh, unsigned int level, const char *path);

char yajl_helper_path_is(yajl_helper_t yh, const char *path);

const char *yajl_helper_get_map_key(yajl_helper_t yh, unsigned int offset);

// void yajl_helper_callbacks_init(yajl_callbacks *callbacks, char nums_as_strings);

size_t json_value_default_string(struct json_value *value, const unsigned char **target,
                                 size_t *len);

// ---- string_list
#ifndef STRING_LIST
#define STRING_LIST
struct string_list {
  struct string_list *next;
  char *value;
};
#endif

// add_string_to_array(): return 1 if added non-null value
char add_string_to_array(struct string_list **head, struct json_value *value);

void string_list_free(struct string_list *e);

/// ---- int_list
struct int_list {
  struct int_list *next;
  long long i;
};

void add_int_to_array(struct int_list **head, struct json_value *value);
void int_list_free(struct int_list *e);

/**
 * Print the current path for e.g. error reporting
 */
void yajl_helper_dump_path(yajl_helper_t yh, FILE *out);

/**
 * Walk the path and apply a user-defined function to each ancestor
 *
 */
void yajl_helper_walk_path(yajl_helper_t yh,
                           void *ctx,
                           void (*func)(void *ctx,
                                        unsigned depth,      // 0-based
                                        char type,           // '[' or '{'
                                        unsigned item_index, // 0-based
                                        const char *map_key) // non-null if type == '{'
                           );

/**
 * Print any error from the yajl parser
 * Returns non-zero
 */
int yajl_helper_print_err(yajl_handle yajl,
                          unsigned char *last_parsed_buff,
                          size_t last_parsed_buff_len
                          );

#endif // ifdef YAJL_HELPER_H
