#ifndef YAJL_HELPER_H
#define YAJL_HELPER_H
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include <stdio.h>

#include "yajl_helper/json_value.h"

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

struct yajl_helper_parse_state {
  unsigned int level;
  unsigned int max_level;

  unsigned int level_offset; // for nested parsing. when > 0, yajl_helper_got_path() will skip the specified number of levels. use yajl_helper_level_offset() to set

  char *stack;
  char **map_keys;
  unsigned int *item_ind;

  yajl_callbacks callbacks;
  yajl_handle yajl;

  void *data; // user-defined

  int (*start_map)(struct yajl_helper_parse_state *);
  int (*end_map)(struct yajl_helper_parse_state *);
  int (*map_key)(struct yajl_helper_parse_state *, const unsigned char *, size_t);
  int (*start_array)(struct yajl_helper_parse_state *);
  int (*end_array)(struct yajl_helper_parse_state *);
  int (*value)(struct yajl_helper_parse_state *, struct json_value *);
};

void yajl_helper_set_data(struct yajl_helper_parse_state *st, void *data);
void *yajl_helper_data(struct yajl_helper_parse_state *st);

unsigned int yajl_helper_level(struct yajl_helper_parse_state *st);

// yajl_helper_level_offset(): return error
int yajl_helper_level_offset(struct yajl_helper_parse_state *st, unsigned int offset);

unsigned int yajl_helper_array_index_plus_1(struct yajl_helper_parse_state *, unsigned int offset);
unsigned int yajl_helper_element_index_plus_1(struct yajl_helper_parse_state *st, unsigned int offset);

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
char yajl_helper_got_path(struct yajl_helper_parse_state *st, unsigned int level, const char *path);
char yajl_helper_got_path_prefix(struct yajl_helper_parse_state *st, unsigned int level, const char *path);

char yajl_helper_path_is(struct yajl_helper_parse_state *st, const char *path);

const char *yajl_helper_get_map_key(struct yajl_helper_parse_state *st, unsigned int offset);

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
                                         );

void yajl_helper_callbacks_init(yajl_callbacks *callbacks, char nums_as_strings);

void yajl_helper_parse_state_free(struct yajl_helper_parse_state *st);


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
void yajl_helper_dump_path(struct yajl_helper_parse_state *st, FILE *out);

/**
 * Print any error from the yajl parser
 * Returns non-zero
 */
int yajl_helper_print_err(yajl_handle yajl,
                          unsigned char *last_parsed_buff,
                          size_t last_parsed_buff_len
                          );

#endif // ifdef YAJL_HELPER_H
