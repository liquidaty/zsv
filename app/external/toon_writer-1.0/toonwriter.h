#ifndef TOONWRITER_H
#define TOONWRITER_H

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#define TOONW_STRLEN(x) strlen((const char *)x)

#ifdef __cplusplus
extern "C" {
#endif

# ifdef _WIN32
#  define toonw_int64 __int64
#  define TOONW_INT64_MIN _I64_MIN
#  define TOONW_INT64_MAX _I64_MAX
#  define TOONW_INT64_PRINTF_FMT "%"PRId64
#  define toonw_uint32 uint32_t
# else // not WIN
#  define toonw_int64 int64_t
#  define TOONW_INT64_MIN INT64_MIN
#  define TOONW_INT64_MAX INT64_MAX
#  define TOONW_INT64_PRINTF_FMT "%" PRId64
#  define toonw_uint32 uint32_t
# endif

  enum toonwriter_option {
    toonwriter_option_pretty = 0,
    toonwriter_option_compact = 1
  };

  enum toonwriter_status {
    toonwriter_status_ok = 0, // no error
    toonwriter_status_out_of_memory = 1,
    toonwriter_status_invalid_value,
    toonwriter_status_misconfiguration,
    toonwriter_status_unrecognized_variant_type,
    toonwriter_status_invalid_end,
    toonwriter_status_io_error // spill temp-file create/read/write failure
  };

  struct toonwriter_data;

  typedef struct toonwriter_data * toonwriter_handle;

  struct toonwriter_opts {
    size_t max_buffer_size; // default to 1MB. This is the amount of heap space used to write an array's elements, before it gets (temporarily) written to disk
    // see /tmp/json2toon/src/store.c
    char *(*get_temp_filename)(const char *prefix); // optional custom function used to get temp file name. return value must be free'd by caller
    unsigned indent; // spaces per indentation level (0 = default 2)
  };

  toonwriter_handle toonwriter_new(FILE *f, struct toonwriter_opts *opts); // opts can be NULL
  toonwriter_handle toonwriter_new_stream(size_t (*write)(const void *restrict, size_t, size_t, void *restrict),
                                          void *write_arg, struct toonwriter_opts *opts);

  void toonwriter_set_option(toonwriter_handle h, enum toonwriter_option opt);
  void toonwriter_flush(toonwriter_handle h);
  void toonwriter_delete(toonwriter_handle h);

  // current sticky error (toonwriter_status_ok if none). Once set, write calls
  // become no-ops. Useful for callers (e.g. write_raw) whose return value is not
  // a status, or to check after a batch of writes.
  enum toonwriter_status toonwriter_error(toonwriter_handle h);

  int toonwriter_start_object(toonwriter_handle h);
  int toonwriter_start_array(toonwriter_handle h);

  enum toonwriter_status toonwriter_end_array(toonwriter_handle h);  // end an error, or return err
  enum toonwriter_status toonwriter_end_object(toonwriter_handle h); // end an object, or return err
  enum toonwriter_status toonwriter_end(toonwriter_handle h); // end either an array or an object, or return err

  // to do: toonwriter_end_array() and toonwriter_end_object(): same as toonwriter_end, but
  // return an error if no matching open array/obj
  int toonwriter_end_all(toonwriter_handle h);

  // write an object key. len_or_zero semantics: a positive len uses exactly that
  // many bytes (the key need not be NUL-terminated and may contain NULs); len 0
  // means "key is a C string" -> strlen(key), except that a NULL key with len 0
  // is the empty key (no strlen). So pass (NULL, 0) -- not ("", 0) -- to emit an
  // empty key from a non-NUL-terminated source.
  int toonwriter_object_keyn(toonwriter_handle data, const char *key, size_t len_or_zero);
  int toonwriter_object_key(toonwriter_handle h, const char *key);
  #define toonwriter_object_str(h, key, v) toonwriter_object_key(h, key), toonwriter_str(h, v)
  #define toonwriter_object_strn(h, key, v, len) toonwriter_object_key(h, key), toonwriter_strn(h, v, len)
  #define toonwriter_object_cstr(h, key, v) toonwriter_object_key(h, key), toonwriter_cstr(h, v)
  #define toonwriter_object_cstrn(h, key, v, len) toonwriter_object_key(h, key), toonwriter_cstrn(h, v, len)
  #define toonwriter_object_bool(h, key, v) toonwriter_object_key(h, key), toonwriter_bool(h, v)
  #define toonwriter_object_dbl(h, key, v)  toonwriter_object_key(h, key), toonwriter_dbl(h, v)
  #define toonwriter_object_dblf(h, key, v, fmt, t) toonwriter_object_key(h, key), toonwriter_dblf(h, v, fmt, t)
  #define toonwriter_object_int(h, key,	v)  toonwriter_object_key(h, key), toonwriter_int(h, v)
  #define toonwriter_object_size_t(h, key,	v)  toonwriter_object_key(h, key), toonwriter_size_t(h, v)
  #define toonwriter_object_null(h, key) toonwriter_object_key(h, key), toonwriter_null(h)
  #define toonwriter_object_array(h, key) toonwriter_object_key(h, key), toonwriter_start_array(h)
  #define toonwriter_object_object(h, key) toonwriter_object_key(h, key), toonwriter_start_object(h)

  int toonwriter_str(toonwriter_handle h, const unsigned char *s);
  int toonwriter_strn(toonwriter_handle h, const unsigned char *s, size_t len);
  int toonwriter_cstr(toonwriter_handle data, const char *s);
  int toonwriter_cstrn(toonwriter_handle data, const char *s, size_t len);
  int toonwriter_bool(toonwriter_handle h, unsigned char value);
  int toonwriter_dbl(toonwriter_handle h, long double d);
  int toonwriter_dblf(toonwriter_handle h, long double d, const char *format_string, unsigned char trim_trailing_zeros_after_dec);

  int toonwriter_size_t(toonwriter_handle data, size_t sz);
  int toonwriter_int(toonwriter_handle h, toonw_int64 i);
  int toonwriter_null(toonwriter_handle h);

  // optionally, you can configure toonwriter to handle custom variant types
  enum toonwriter_datatype {
    toonwriter_datatype_null = 0,
    toonwriter_datatype_string = 1,
    toonwriter_datatype_integer = 2,
    toonwriter_datatype_float = 3,
    toonwriter_datatype_bool = 4,
    toonwriter_datatype_raw = 5, // already stringified, output verbatim
    // possible to do:
    //  array
    //  object
  };

  struct toonwriter_variant {
    enum toonwriter_datatype type;
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

  // toonwriter_set_variant_handler(): provide toonwriter with a custom function that converts your data to a toonwriter_variant
  // with an optional cleanup callback
  // returns 0 on success, nonzero on error
  enum toonwriter_status
  toonwriter_set_variant_handler(toonwriter_handle h,
                                 struct toonwriter_variant (*to_toonw_variant)(void *),
                                 void (*cleanup)(void *, struct toonwriter_variant *));

  // write a variant. will use custom to_toonw_variant() to convert data to toonwriter_variant
  enum toonwriter_status toonwriter_variant(toonwriter_handle h, void *data);

  // write raw data to the output stream. Note: caller is responsible for ensuring that
  // the raw data is valid TOON
  size_t toonwriter_write_raw(toonwriter_handle toonw, const unsigned char *s, size_t len);

  /*
   * Write a value of unknown datatype. If it is numeric or bool
   * conforming to RFC 8259, write it as-is; otherwise treat it
   * as a string and write the stringified value
   */
  int toonwriter_unknown(toonwriter_handle h, const unsigned char *s, size_t len, toonw_uint32 flags);

#ifdef __cplusplus
}
#endif

#endif // #ifndef TOONWRITER_H
