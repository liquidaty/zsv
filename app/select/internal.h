#include <sys/types.h> // Required for off_t

#ifndef ZSV_NO_PARALLEL
#include "parallel.h"
#endif

#define ZSV_SELECT_MAX_COLS_DEFAULT 1024
#define ZSV_SELECT_MAX_COLS_DEFAULT_S "1024"

struct zsv_select_search_str {
  struct zsv_select_search_str *next;
  const char *value;
  size_t len;
};

struct zsv_select_uint_list {
  struct zsv_select_uint_list *next;
  unsigned int value;
};

struct fixed {
  size_t *offsets;
  size_t count;
  size_t max_lines; // max lines to use to calculate offsets
  char autodetect;
};

struct zsv_select_data {
  const char *input_path;
  unsigned int current_column_ix;
  size_t data_row_count;

  struct zsv_opts *opts;
  zsv_parser parser;
  unsigned int errcount;

  unsigned int output_col_index; // num of cols printed in current row

  // output columns:
  const char **col_argv;
  int col_argc;
  char *cols_to_print; // better: bitfield

  struct {
    unsigned int ix; // index of the input column to be output
    struct {         // merge data: only used with --merge
      struct zsv_select_uint_list *indexes, **last_index;
    } merge;
  } *out2in; // array of .output_cols_count length; out2in[x] = y where x = output ix, y = input info

  unsigned int output_cols_count; // total count of output columns

#define MAX_EXCLUSIONS 1024
  const unsigned char *exclusions[MAX_EXCLUSIONS];
  unsigned int exclusion_count;

  unsigned int header_name_count;
  unsigned char **header_names;

  const char *prepend_header; // --prepend-header

  char header_finished;

  char embedded_lineend;

  double sample_pct;

  unsigned sample_every_n;

  size_t data_rows_limit;
  size_t skip_data_rows;

  struct zsv_select_search_str *search_strings;
#ifdef HAVE_PCRE2_8
  struct zsv_select_regex *search_regexs;
#endif

  zsv_csv_writer csv_writer;

  size_t overflow_size;

  struct fixed fixed;

#ifndef ZSV_NO_PARALLEL
  unsigned num_chunks;
  off_t end_offset_limit;                  // Byte offset where the current parser instance should stop
  off_t next_row_start;                    // Actual byte offset of the last row that was processed
  struct zsv_parallel_data *parallel_data; // Pointer to the thread management structure
#endif

  // FLAGS
  unsigned char whitespace_clean_flags;

  unsigned char print_all_cols : 1;
  unsigned char use_header_indexes : 1;
  unsigned char no_trim_whitespace : 1;
  unsigned char cancelled : 1;
  unsigned char skip_this_row : 1;
  unsigned char verbose : 1;
  unsigned char clean_white : 1;
  unsigned char prepend_line_number : 1;

  unsigned char any_clean : 1;
#define ZSV_SELECT_DISTINCT_MERGE 2
  unsigned char distinct : 2; // 1 = ignore subsequent cols, ZSV_SELECT_DISTINCT_MERGE = merge subsequent cols (first
                              // non-null value)
  unsigned char unescape : 1;
  unsigned char no_header : 1;       // --no-header
  unsigned char run_in_parallel : 1; // Flag if parallel mode is active
  unsigned char _ : 2;               // Reduced padding by 1 bit due to addition of run_in_parallel
};

enum zsv_select_column_index_selection_type {
  zsv_select_column_index_selection_type_none = 0,
  zsv_select_column_index_selection_type_single,
  zsv_select_column_index_selection_type_range,
  zsv_select_column_index_selection_type_lower_bounded
};
