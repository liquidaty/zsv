#ifndef JSON_VALUE_H
#define JSON_VALUE_H

struct json_value {
  enum {
    json_value_null = 1,
    json_value_bool,
    json_value_int,
    json_value_string,
    json_value_double,
    json_value_number_string,
    json_value_error
  } type;

  union {
    unsigned char *s;
    char *cs;
    long long i;
    double dbl;
  } val;

  size_t strlen;
};

#define json_value_free(value) do { \
    if((value)->type == json_value_string && (value)->val.s) free((value)->val.s), (value)->type = json_value_null; \
  } while(0)

#define json_value_dup(dest, src) do {                          \
    memcpy(dest, src, sizeof(*dest));                           \
    if((src)->type == json_value_string && (src)->val.cs) {     \
      size_t sz = strlen((src)->val.cs);                        \
      if(((dest)->val.cs = malloc(sz + 2))) {                   \
        memcpy((dest)->val.cs, (src)->val.cs, sz);              \
        (dest)->val.cs[sz] = (dest)->val.cs[sz+1] = '\0';       \
      }                                                         \
    }                                                           \
  } while(0)

#define json_value_to_str(value) do {                 \
    if((value)->type != json_value_string) {            \
      char *s;                                          \
      if((value)->type == json_value_null) {            \
        s = calloc(1, 3);                               \
      } else if((value)->type == json_value_double) {   \
        asprintf(&s, "%lf", (value)->val.dbl);          \
      } else {                                          \
        asprintf(&s, "%lli", (value)->val.i);           \
      }                                                 \
      (value)->val.cs = s;                              \
      (value)->strlen = strlen(s);                      \
      (value)->type = json_value_string;                \
    }                                                   \
  } while(0)

void json_value_delete(struct json_value **valuep);
struct json_value *json_value_copy(struct json_value *value);

#endif
