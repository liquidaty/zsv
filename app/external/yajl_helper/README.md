# yajl_helper

Helper library to make it easier to write streaming JSON parsers using yajl

# why yajl, and why not yajl by itself?

yajl is fast and memory efficient. You can read more about its advantages at https://github.com/lloyd/yajl

For many use cases, yajl is fast enough that from a holistic perspective, any performance
bottleneck, when it comes to speed, is not yajl.

However, the performance comes with a cost, in that writing a parser with yajl can be difficult
and time consuming, and require much more code than with other parsers.

# purpose

The purpose of this library is to make it less burdensome to write a yajl JSON parser,
at a small cost of speed and no cost to memory (other than a small constant-sized initial
allocation).

The primary way this is achieved is by tracking the "path" of the current location
of the parser so that you don't have to, and providing some convenience functions
to query the path when a value is submitted for processing.

Advantages:

* Allow you to write yajl parser with fewer lines of code that are easier to write,
easier to read and easier to maintain

* No additional memory overhead, other than a small, initial, fixed-size heap allocation

Disadvantage:

* Some additional, but small and fixed, CPU overhead 

# usage:

## Without `yajl_helper`

When you use yajl "out of the box", you do something like this. One of the main challenges of
this step is that you have to keep track of where you are in your JSON object

```
  /********************************************************************
   * my custom handlers
   * I have to keep track myself of where in my JSON object I am
   * currently processing, probably with a stack
   * (e.g. which objects property was just read as 'true'?)
   ********************************************************************/

  static int my_handler_null(void *ctx);
  static int my_handler_map_key(void *ctx, const unsigned char *key, size_t len) {
    // example of a linear position tracker
    if(!strncmp(strncmp(key, "contacts", len))) {
      location_push(ctx, "contacts");
    } else if(my_tracker->current_location == contacts_obj && !strncmp(strncmp(key, "firstName", len))) {
      location_push(ctx, "firstName");
    }
    ...
  }
  static int my_handler_string(void *ctx, const unsigned char *stringVal, size_t len) {
    if(location_check("contacts", "firstName"))
      contacts_set_first_name(ctx, stringVal, len);
    ...
  }

  /* ... */

  yajl_callbacks callbacks = {
    my_handler_null,
    my_handler_bool,
    my_handler_int,
    my_handler_double,
    NULL, /* or alternatively, my_handler_number */
    my_handler_string,
    my_handler_start_map,
    my_handler_map_key,
    my_handler_end_map,
    my_handler_start_array,
    my_handler_end_array
  };

  /* parse your input */
  void *ctx;
  yajl_handle hand = yajl_alloc(&callbacks, NULL, (void *) ctx);
  char buff[4096];
  yajl_status stat = yajl_status_ok;
  size_t bytes_read;

  while(stat == yajl_status_ok && (bytes_read = fread(buff, 1, sizeof(buff), stdin)) > 0)
    stat = yajl_parse(hand, buff, bytes_read);

  yajl_free(hand);
```

## With yajl_helper
yajl_helper handles the location tracking with a fixed-size stack
(and a little heap mem to copy map key names), and consolidates all
processing of scalar values into a single function call. Now I really
just have to write that one value-processing call:

```
static int got_scalar_value(struct yajl_helper_parse_state *st, struct json_value *value) {
  struct my_custom_data *data = st->data;
  struct json_value_string jvs;

  if(yajl_helper_path_is(st, "{contacts{firstName")) // or, for faster performance, use yajl_helper_got_path()
    json_value_to_string_dup(value, &data->currentRecord.firstName);
  else if(yajl_helper_path_is(st, 2, "{contacts{lastName"))
    json_value_to_string_dup(value, &data->currentRecord.lastName);
  else if(yajl_helper_path_is(st, "{*{*"))
    fprintf(stderr, "Got something unexpected: %.*s\n", json_value_to_string(value, &jvs, 1), jvs.s);
  else {
    fprintf(stderr, "Ignoring: %.*s", json_value_to_string(value, &jvs, 1), jvs.s);
  return 1;
}

```

# to do:

* support escape chars, in case you want to match the literal string "*" as a match key
