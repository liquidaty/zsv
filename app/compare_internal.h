#ifndef ZSV_COMPARE_PRIVATE_H
#define ZSV_COMPARE_PRIVATE_H

#include <sglib.h>

typedef struct zsv_compare_unique_colname {
  struct zsv_compare_unique_colname *left;
  struct zsv_compare_unique_colname *right;
  struct zsv_compare_unique_colname *next;
  unsigned char *name;
  size_t name_len;
  unsigned instance_num;
  unsigned output_ix; // only used for output columns
  unsigned char color;
  unsigned char is_key;
} zsv_compare_unique_colname;

SGLIB_DEFINE_RBTREE_PROTOTYPES(zsv_compare_unique_colname, left, right, color, zsv_compare_unique_colname_cmp);
static void zsv_compare_unique_colnames_delete(zsv_compare_unique_colname **tree);

struct zsv_compare_input_key {
  struct zsv_compare_key *key;
  struct zsv_cell value;
  unsigned col_ix;
  unsigned char found;
  unsigned char is_key;
};

struct zsv_compare_input {
  const char *path;
  FILE *stream;
  zsv_parser parser;
  zsv_compare_unique_colname *colnames;
  unsigned index; // order in which this input was added

  unsigned col_count;
  unsigned *out2in; // out2in[output column ix] = input column ix + 1 (zero for no match)
  zsv_compare_unique_colname **output_colnames; // colname_ptrs;

  unsigned key_count;
  struct zsv_compare_input_key *keys;

  unsigned char row_loaded:1;
  unsigned char done:1;
  unsigned char _:6;
};

struct zsv_compare_key {
  struct zsv_compare_key *next;
  const char *name;
  unsigned position_plus_1; // position can be specified in lieu of name
};

struct zsv_compare_data {
  enum zsv_compare_status status;
  unsigned input_count; // number of allocated compare_input structs
  struct zsv_compare_input *inputs;
  struct zsv_compare_input **inputs_to_sort;

  unsigned key_count;
  struct zsv_compare_key *keys;

  size_t row_count; // only matters if no ID columns are specified

  unsigned output_colcount;
  zsv_compare_unique_colname *output_colnames; // tree
  zsv_compare_unique_colname **output_colnames_next;
  zsv_compare_unique_colname *output_colnames_first; // linked list

  zsv_compare_cell_func cmp;
  void *cmp_ctx;

//  void (*unmatched_row_handler)(void *ctx, struct zsv_compare_input *key_input,
//                                struct zsv_compare_input *unmatched_row_input);
//  void *unmatched_row_handler_ctx;
  struct {
    char type; // 'j' for json
    union {
      zsv_csv_writer csv;
      jsonwriter_handle jsw;
    } handle;
  } writer;

  unsigned char allow_duplicate_column_names:1;
  unsigned char _:7;
};

#endif
