/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 */

/**
 * \file yatl_parse.h
 * Interface to yatl's TOON stream-parsing facilities.
 *
 * yatl is an event-driven (SAX) parser: it consumes TOON text in arbitrary
 * chunks and invokes client callbacks as values are recognized. TOON encodes
 * the JSON data model, so the callback set is the JSON one -- a tabular array
 * `users[2]{id,name}:` is reported exactly as an array of two objects.
 */

#include <yatl/yatl_common.h>

#ifndef __YATL_PARSE_H__
#define __YATL_PARSE_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
    /** error codes returned from this interface */
    typedef enum {
        /** no error was encountered */
        yatl_status_ok,
        /** a client callback returned zero, stopping the parse */
        yatl_status_client_canceled,
        /** an error occurred during the parse. Call yatl_get_error for more
         *  information about the encountered error */
        yatl_status_error
    } yatl_status;

    /** attain a human readable, english, string for an error */
    YATL_API const char * yatl_status_to_string(yatl_status code);

    /** an opaque handle to a parser */
    typedef struct yatl_handle_t * yatl_handle;

    /** yatl is an event driven parser. as TOON elements are parsed, the
     *  callbacks below are invoked. Each accepts the "context" pointer passed
     *  to yatl_alloc, which client code may use to carry state.
     *
     *  All callbacks return an integer. If non-zero the parse continues; if
     *  zero the parse is canceled and yatl_status_client_canceled is returned.
     *
     *  Number handling mirrors yajl: yatl converts numbers representable in a
     *  long long (yatl_integer) or a double (yatl_double). If yatl_number is
     *  non-NULL it is used for all numbers instead, receiving the verbatim
     *  TOON text. If yatl_number is NULL and a number exceeds the range of the
     *  corresponding type, the optional yatl_error callback is invoked, or a
     *  parse error is raised if it too is NULL.
     */
    typedef struct {
        int (* yatl_null)(void * ctx);
        int (* yatl_boolean)(void * ctx, int boolVal);
        int (* yatl_integer)(void * ctx, long long integerVal);
        int (* yatl_double)(void * ctx, double doubleVal);
        /** the verbatim text of a number; used for all numbers when present */
        int (* yatl_number)(void * ctx, const char * numberVal,
                            size_t numberLen);

        /** strings point into the parser's decode buffer or the input text and
         *  are _not_ NUL terminated */
        int (* yatl_string)(void * ctx, const unsigned char * stringVal,
                            size_t stringLen);

        int (* yatl_start_map)(void * ctx);
        int (* yatl_map_key)(void * ctx, const unsigned char * key,
                             size_t keyLen);
        int (* yatl_end_map)(void * ctx);

        int (* yatl_start_array)(void * ctx);
        int (* yatl_end_array)(void * ctx);

        /** invoked when a number is out of range (errno passed in err_no) and
         *  yatl_number is NULL; may be NULL */
        int (* yatl_error)(void * ctx, const unsigned char * buf, size_t len,
                           int err_no);
    } yatl_callbacks;

    /** allocate a parser handle
     *  \param callbacks  a yatl_callbacks structure specifying the functions
     *                    to call as TOON entities are encountered. May be NULL,
     *                    which is only useful for validation.
     *  \param afs        memory allocation functions, may be NULL to use the
     *                    C runtime routines (malloc and friends)
     *  \param ctx        a context pointer passed to the callbacks. */
    YATL_API yatl_handle yatl_alloc(const yatl_callbacks * callbacks,
                                    yatl_alloc_funcs * afs,
                                    void * ctx);

    /** swap parser callback routines. returns the old callback structure
     *  \param handle     a handle allocated with yatl_alloc
     *  \param callbacks  the new callbacks to install
     *  \param new_ctx    the new context pointer to pass to callbacks */
    YATL_API const yatl_callbacks * yatl_swap_callbacks(yatl_handle handle,
                                                        const yatl_callbacks * callbacks,
                                                        void * new_ctx);

    /** configuration parameters for the parser, passed to yatl_config() along
     *  with option specific argument(s). All options default to *off*. */
    typedef enum {
        /** By default the parser verifies that all strings (and keys) are valid
         *  UTF-8 and raises a parse error otherwise. Set to disable that check.
         *    yatl_config(h, yatl_dont_validate_strings, 1);
         */
        yatl_dont_validate_strings = 0x01,
        /** By default yatl_complete_parse() requires the entire input to form a
         *  single complete document and raises an error on trailing content.
         *  Set to suppress that check. */
        yatl_allow_trailing_garbage = 0x02,
        /** Accept any unquoted token as a bare string instead of rejecting
         *  tokens the encoder would have quoted (default: strict, so non-TOON
         *  junk is a parse error). */
        yatl_lenient_scalars = 0x04,
        /** By default an array's declared element count "[N]" is validated
         *  against the number of elements actually present. Set to skip it. */
        yatl_dont_validate_length = 0x08,
        /** Maximum container-nesting depth accepted before a parse error is
         *  raised (default YATL_MAX_DEPTH). Unlike the flags above this option
         *  takes an unsigned argument rather than an on/off toggle:
         *    yatl_config(h, yatl_max_depth, (unsigned)256);
         *  A zero argument is rejected (yatl_config returns 0). */
        yatl_max_depth = 0x10
    } yatl_option;

    /** allow the modification of parser options after handle allocation.
     *  \returns zero in case of errors, non-zero otherwise */
    YATL_API int yatl_config(yatl_handle h, yatl_option opt, ...);

    /** free a parser handle */
    YATL_API void yatl_free(yatl_handle handle);

    /** Parse some TOON!
     *  \param hand        a handle allocated with yatl_alloc
     *  \param toonText    a pointer to the UTF-8 TOON text to be parsed
     *  \param toonTextLen the length, in bytes, of the input text */
    YATL_API yatl_status yatl_parse(yatl_handle hand,
                                    const unsigned char * toonText,
                                    size_t toonTextLen);

    /** Parse any remaining buffered TOON. yatl is line-oriented, so the final
     *  line of input is not acted upon until a terminating newline or this call
     *  arrives; yatl_complete_parse() flushes it and closes open containers.
     *  \param hand        a handle allocated with yatl_alloc */
    YATL_API yatl_status yatl_complete_parse(yatl_handle hand);

    /** get an error string describing the state of the parse.
     *
     *  If verbose is non-zero, the message includes the TOON text where the
     *  error occurred, with an arrow pointing to the offending byte.
     *
     *  \returns a dynamically allocated string that should be freed with
     *  yatl_free_error */
    YATL_API unsigned char * yatl_get_error(yatl_handle hand, int verbose,
                                            const unsigned char * toonText,
                                            size_t toonTextLen);

    /** get the amount of data consumed from the last chunk passed to yatl.
     *  On a successful parse this reports whether the buffer was fully
     *  consumed; on an error it is the byte offset at which the error was
     *  detected. */
    YATL_API size_t yatl_get_bytes_consumed(yatl_handle hand);

    /** free an error returned from yatl_get_error */
    YATL_API void yatl_free_error(yatl_handle hand, unsigned char * str);

#ifdef __cplusplus
}
#endif

#endif
