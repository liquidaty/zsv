/**
 * Simple example using libzsv to call a row handler after each row is parsed
 *
 * In this example, we will use libzsv to parse a CSV file, look for a specified
 * column of data, and for each row of data, output only that column
 *
 * Example:
 *   `echo "hi,there,you\na,b,c\nd,e,f" | ./print_my_column there
 * Outputs:
 *   there
 *
 * In our implementation, we will define two row handlers. The first handler
 * will be used to handle the first row, which we assume to be the header,
 * and will search for the target column to identify what position it is in
 * (or whether we can't find it). The second handler will be used to process
 * each data row, and will output the target column
 */
static void print_my_column(void *ctx);
static void find_my_column(void *ctx);

#include <stdio.h>
#include <zsv.h>
#include <string.h>

/**
 * First, we define a structure that will contain all the information that
 * our row handler will need
 */
struct my_data {
  zsv_parser parser;
  const char *target_column_name; /* name of column to find and output */
  size_t target_column_position;  /* will be set to the position of the column to output */
  char not_found;                 /* if we can't find the column, we'll set this flag */
};

/**
 * Our first callback will search each cell in the (header) row. If it finds a match
 * it sets the `target_column_position`, otherwise it halts further processing and
 * sets the `not_found` flag
 */
static void find_my_column(void *ctx) {
  struct my_data *data = ctx;
  size_t target_column_name_len = strlen(data->target_column_name);

  /* iterate through each cell */
  size_t cell_count = zsv_cell_count(data->parser);
  char found = 0;
  for(size_t i = 0; i < cell_count; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    if(c.len == target_column_name_len && !memcmp(data->target_column_name, c.str, c.len)) {
      data->target_column_position = i;
      found = 1;
      break;
    }
  }

  if(!found) {
    /**
     * Abort if we couldn't find the target column name in our header row
     * by calling `zsv_abort()`
     */
    fprintf(stderr, "Could not find column %.*s\n", (int)target_column_name_len, data->target_column_name);
    zsv_abort(data->parser);
    data->not_found = 1;
  } else {
    /**
     * we found the column we are looking for. print its name, and change our row callback
     * by calling `zsv_set_row_handler()`
     */
    printf("%s\n", data->target_column_name);
    zsv_set_row_handler(data->parser, print_my_column);
  }
}

/**
 * Output data from the selected column
 */
static void print_my_column(void *ctx) {
  struct my_data *data = ctx;
  struct zsv_cell c = zsv_get_cell(data->parser, data->target_column_position);
  printf("%.*s\n", (int)c.len, c.str);
}


int main(int argc, const char *argv[]) {
  struct my_data data = { 0 };
  struct zsv_opts opts = { 0 };
  opts.row_handler = find_my_column;
  opts.ctx = &data;

  if(argc < 2)
    return fprintf(stderr, "Usage: print_my_column column_name < input.csv\n");

  data.target_column_name = argv[1];
  data.parser = zsv_new(&opts);
  while(zsv_parse_more(data.parser) == zsv_status_ok)
    ;
  zsv_finish(data.parser);
  zsv_delete(data.parser);

  return data.not_found;
}
