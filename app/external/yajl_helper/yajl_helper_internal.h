struct yajl_helper_parse_state { // TO DO: make this opaque
  unsigned int level;
  unsigned int max_level;

  unsigned int level_offset; // for nested parsing. when > 0, yajl_helper_got_path() will skip the specified number of levels. use yajl_helper_level_offset() to set

  char *stack;
  char **map_keys;
  unsigned int *item_ind;

  yajl_callbacks callbacks;
  yajl_handle yajl;

  void *data; // user-defined

  // yajl callbacks: return 1 on success (continue), 0 to abort
  int (*start_map)(struct yajl_helper_parse_state *);
  int (*end_map)(struct yajl_helper_parse_state *);
  int (*map_key)(struct yajl_helper_parse_state *, const unsigned char *, size_t);
  int (*start_array)(struct yajl_helper_parse_state *);
  int (*end_array)(struct yajl_helper_parse_state *);
  int (*value)(struct yajl_helper_parse_state *, struct json_value *);
};
