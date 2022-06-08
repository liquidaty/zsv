#ifndef JSONWRITER_H
#define JSONWRITER_H

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#define JSW_STRLEN(x) strlen((const char *)x)

#ifdef __cplusplus
extern "C" {
#endif

# ifdef _WIN32
#  define jsw_int64 __int64
#  define JSW_INT64_MIN _I64_MIN
#  define JSW_INT64_MAX _I64_MAX
#  define JSW_INT64_PRINTF_FMT "%"PRId64
# else // not WIN
#  define jsw_int64 int64_t
#  define JSW_INT64_MIN INT64_MIN
#  define JSW_INT64_MAX INT64_MAX
#  define JSW_INT64_PRINTF_FMT "%"PRId64
# endif

  enum jsonwriter_option {
    jsonwriter_option_pretty = 0,
    jsonwriter_option_compact = 1
  };

  enum jsonwriter_status {
    jsonwriter_status_ok = 0, // no error
    jsonwriter_status_out_of_memory = 1,
    jsonwriter_status_invalid_value,
    jsonwriter_status_misconfiguration,
    jsonwriter_status_unrecognized_variant_type,
    jsonwriter_status_invalid_end
  };

  struct jsonwriter_data;

  typedef struct jsonwriter_data * jsonwriter_handle;

  jsonwriter_handle jsonwriter_new(FILE *f);
  jsonwriter_handle jsonwriter_new_stream(size_t (*write)(const void *restrict, size_t, size_t, void *restrict),
                                          void *write_arg);

  void jsonwriter_set_option(jsonwriter_handle h, enum jsonwriter_option opt);
  void jsonwriter_flush(jsonwriter_handle h);
  void jsonwriter_delete(jsonwriter_handle h);

  int jsonwriter_start_object(jsonwriter_handle h);
  int jsonwriter_start_array(jsonwriter_handle h);

  enum jsonwriter_status jsonwriter_end_array(jsonwriter_handle h);  // end an error, or return err
  enum jsonwriter_status jsonwriter_end_object(jsonwriter_handle h); // end an object, or return err
  enum jsonwriter_status jsonwriter_end(jsonwriter_handle h); // end either an array or an object, or return err

  // to do: jsonwriter_end_array() and jsonwriter_end_object(): same as jsonwriter_end, but
  // return an error if no matching open array/obj
  int jsonwriter_end_all(jsonwriter_handle h);

  int jsonwriter_object_keyn(jsonwriter_handle data, const char *key, size_t len_or_zero);
  int jsonwriter_object_key(jsonwriter_handle h, const char *key);
  #define jsonwriter_object_str(h, key, v) jsonwriter_object_key(h, key), jsonwriter_str(h, v)
  #define jsonwriter_object_strn(h, key, v, len) jsonwriter_object_key(h, key), jsonwriter_strn(h, v, len)
  #define jsonwriter_object_cstr(h, key, v) jsonwriter_object_key(h, key), jsonwriter_cstr(h, v)
  #define jsonwriter_object_cstrn(h, key, v, len) jsonwriter_object_key(h, key), jsonwriter_cstrn(h, v, len)
  #define jsonwriter_object_bool(h, key, v) jsonwriter_object_key(h, key), jsonwriter_bool(h, v)
  #define jsonwriter_object_dbl(h, key, v)  jsonwriter_object_key(h, key), jsonwriter_dbl(h, v)
  #define jsonwriter_object_dblf(h, key, v, fmt, t) jsonwriter_object_key(h, key), jsonwriter_dblf(h, v, f, t)
  #define jsonwriter_object_int(h, key,	v)  jsonwriter_object_key(h, key), jsonwriter_int(h, v)
  #define jsonwriter_object_null(h, key) jsonwriter_object_key(h, key), jsonwriter_null(h)
  #define jsonwriter_object_array(h, key) jsonwriter_object_key(h, key), jsonwriter_start_array(h)
  #define jsonwriter_object_object(h, key) jsonwriter_object_key(h, key), jsonwriter_start_object(h)

  int jsonwriter_str(jsonwriter_handle h, const unsigned char *s);
  int jsonwriter_strn(jsonwriter_handle h, const unsigned char *s, size_t len);
  int jsonwriter_cstr(jsonwriter_handle data, const char *s);
  int jsonwriter_cstrn(jsonwriter_handle data, const char *s, size_t len);
  int jsonwriter_bool(jsonwriter_handle h, unsigned char value);
  int jsonwriter_dbl(jsonwriter_handle h, long double d);
  int jsonwriter_dblf(jsonwriter_handle h, long double d, const char *format_string, unsigned char trim_trailing_zeros_after_dec);

  int jsonwriter_int(jsonwriter_handle h, jsw_int64 i);
  int jsonwriter_null(jsonwriter_handle h);

  // optionally, you can configure jsonwriter to handle custom variant types
  enum jsonwriter_datatype {
    jsonwriter_datatype_null = 0,
    jsonwriter_datatype_string = 1,
    jsonwriter_datatype_integer = 2,
    jsonwriter_datatype_float = 3,
    jsonwriter_datatype_bool = 4
    // possible to do:
    //  array
    //  object
  };

  struct jsonwriter_variant {
    enum jsonwriter_datatype type;
    union {
      unsigned char b; // bool
      long int i;      // integer
      long double dbl; // double
      unsigned char *str;       // string
      // possible to do:
      //   void *array; // will require corresponding function pointers to get array size and elements
      //   void *object; // will require corresponding function pointers to get object keys and elements
    } value;
  };

  // jsonwriter_set_variant_handler(): provide jsonwriter with a custom function that converts your data to a jsonwriter_variant
  // with an optional cleanup callback
  // returns 0 on success, nonzero on error
  enum jsonwriter_status
  jsonwriter_set_variant_handler(jsonwriter_handle h,
                                 struct jsonwriter_variant (*to_jsw_variant)(void *),
                                 void (*cleanup)(void *, struct jsonwriter_variant *));

  // write a variant. will use custom to_jsw_variant() to convert data to jsonwriter_variant
  enum jsonwriter_status jsonwriter_variant(jsonwriter_handle h, void *data);

#ifdef __cplusplus
}
#endif

#endif // #ifndef JSONWRITER_H
