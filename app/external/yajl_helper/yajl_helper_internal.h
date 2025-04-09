struct yajl_helper_parse_state { // TO DO: make this opaque
  struct yajl_helper_parse_state *parent; // non-null if this is a nested parser
  unsigned int level;
  unsigned int max_level;

  unsigned int level_offset; // for nested parsing. when > 0, yajl_helper_got_path() will skip the specified number of levels. use yajl_helper_level_offset() to set

  char *stack;
  char **map_keys;
  unsigned int *item_ind;

  yajl_callbacks callbacks;
  yajl_handle yajl;

  void *ctx; // user-defined
  void (*ctx_destructor)(void *); // optional destructor for user-defined

  // internal use only
  void (*error)(struct yajl_helper_parse_state *);

  // yajl callbacks: return 1 on success (continue), 0 to abort
  int (*start_map)(struct yajl_helper_parse_state *);
  int (*end_map)(struct yajl_helper_parse_state *);
  int (*map_key)(struct yajl_helper_parse_state *, const unsigned char *, size_t);
  int (*start_array)(struct yajl_helper_parse_state *);
  int (*end_array)(struct yajl_helper_parse_state *);
  int (*value)(struct yajl_helper_parse_state *, struct json_value *);
};
