#ifndef ZSV_COMPARE_PRIVATE_H
#define ZSV_COMPARE_PRIVATE_H

#include <sglib.h>
#include <sqlite3.h>

typedef struct zsv_compare_unique_colname {
  struct zsv_compare_unique_colname *next; // retain order via linked list

  // name and instance_num must be unique
  unsigned char *name;
  size_t name_len;
  unsigned instance_num;

  // keep track of how many instances we've seen in total
  unsigned total_instances; // only applies if instance_num == 0

  unsigned output_ix; // only used for output columns

  struct zsv_compare_unique_colname *left;
  struct zsv_compare_unique_colname *right;
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
  zsv_compare_unique_colname **output_colnames;

  unsigned key_count;
  struct zsv_compare_input_key *keys;

  sqlite3_stmt *sort_stmt;

  unsigned char row_loaded:1;
  unsigned char missing:1;
  unsigned char done:1;
  unsigned char _:5;
};

struct zsv_compare_key {
  struct zsv_compare_key *next;
  const char *name;
  unsigned position_plus_1; // position can be specified in lieu of name
};

struct zsv_compare_added_column {
  struct zsv_compare_added_column *next;
  struct zsv_compare_unique_colname *colname;
  struct zsv_compare_unique_colname *output_colname;
  struct zsv_compare_input *input;
  unsigned col_ix; // index of column in input from which to extract this value
};

struct zsv_compare_data {
  enum zsv_compare_status status;
  unsigned input_count; // number of allocated compare_input structs
  struct zsv_compare_input *inputs;
  struct zsv_compare_input **inputs_to_sort;

  unsigned key_count;
  struct zsv_compare_key *keys;
  unsigned char *combined_key_names;

  size_t row_count; // only matters if no ID columns are specified

  unsigned output_colcount;
  zsv_compare_unique_colname *output_colnames; // tree
  zsv_compare_unique_colname **output_colnames_next;
  zsv_compare_unique_colname *output_colnames_first; // linked list

  struct zsv_compare_added_column *added_columns;
  zsv_compare_unique_colname *added_colnames;
  unsigned added_colcount;

  zsv_compare_cell_func cmp;
  void *cmp_ctx;

  enum zsv_status (*next_row)(struct zsv_compare_input *input);
  struct zsv_cell (*get_cell)(struct zsv_compare_input *input, unsigned ix);
  struct zsv_cell (*get_column_name)(struct zsv_compare_input *input, unsigned ix);
  unsigned (*get_column_count)(struct zsv_compare_input *input);
  enum zsv_compare_status (*input_init)(struct zsv_compare_data *data,
                                        struct zsv_compare_input *input,
                                        struct zsv_opts *opts,
                                        struct zsv_prop_handler *custom_prop_handler,
                                        const char *opts_used);

  sqlite3 *sort_db; // used when --sort option was specified

  struct {
    double value;
#define ZSV_COMPARE_MAX_NUMBER_BUFF_LEN 128
    char   str1[ZSV_COMPARE_MAX_NUMBER_BUFF_LEN];
    char   str2[ZSV_COMPARE_MAX_NUMBER_BUFF_LEN];
  } tolerance;
  struct {
    char type; // 'j' for json
    union {
      zsv_csv_writer csv;
      jsonwriter_handle jsw;
    } handle;

    struct {
      unsigned used;
      unsigned allocated;
      char **names;
    } properties;

    unsigned cell_ix;        // only used for json + object output
    unsigned char compact:1; // whether to output compact JSON
    unsigned char object:1;  // whether to output JSON as objects
    unsigned char _:6;
  } writer;

  unsigned char sort:1;
  unsigned char sort_in_memory:1;
  unsigned char print_key_col_names:1;
  unsigned char _:5;
};

#endif
