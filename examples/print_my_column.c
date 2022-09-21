/**
 * Example program: reads CSV rows, outputs number of non-blank cells
 * Example compilation command:
 *   gcc -O3 -o print_my_column print_my_column.c -lzsv -DZSV_EXTRAS
 *   (Note: remove -DZSV_EXTRAS if you configured/compiled/installed libzsv using --minimal=yes)
 * Example:
 *   `echo "hi,there,you\na,b,c\nd,e,f" | ./print_my_column there
 * Outputs:
 *   there
 *   b
 *   e
 */

#include <stdio.h>
#include <zsv.h>
#include <string.h>

struct my_data {
  zsv_parser parser;
  size_t column_to_find_len;
  const char *column_to_find;
  size_t column_to_find_position; // 1-based
  char aborted;
};

/**
 * Output data from the selected column
 */
void print_my_column(void *ctx) {
  struct my_data *data = ctx;
  struct zsv_cell c = zsv_get_cell(data->parser, data->column_to_find_position - 1);
  printf("%.*s\n", (int)c.len, c.str);
}

/**
 * In the first row, find the position of the column I want to output
 * Stop with an error message if not found
 */
void find_my_column(void *ctx) {
  struct my_data *data = ctx;
  size_t column_count = zsv_column_count(data->parser);
  for(size_t i = 0; i < column_count; i++) {
    struct zsv_cell c = zsv_get_cell(data->parser, i);
    if(c.len == data->column_to_find_len && !memcmp(data->column_to_find, c.str, c.len)) {
      data->column_to_find_position = i + 1;
      break;
    }
  }
  if(!data->column_to_find_position) {
    fprintf(stderr, "Could not find column %.*s\n", (int)data->column_to_find_len, data->column_to_find);
    zsv_abort(data->parser);
    data->aborted = 1;
  } else {
    printf("%s\n", data->column_to_find);
    zsv_set_row_handler(data->parser, print_my_column);
  }
}

int main(int argc, const char *argv[]) {
  struct my_data data = { 0 };
  struct zsv_opts opts = { 0 };
  opts.row = find_my_column;
  opts.ctx = &data;

  if(argc < 2)
    return fprintf(stderr, "Usage: print_my_column column_name < input.csv\n");

  data.column_to_find = argv[1];
  data.column_to_find_len = strlen(data.column_to_find);

  data.parser = zsv_new(&opts);
  while(zsv_parse_more(data.parser) == zsv_status_ok)
    ;
  zsv_finish(data.parser);
  zsv_delete(data.parser);

  return data.aborted;
}
