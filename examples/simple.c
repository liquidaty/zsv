#include <stdio.h>
#include <zsv.h>

struct my_data {
  zsv_parser parser;
  size_t rows;
};

void my_row_handler(void *ctx) {
  struct my_data *data = ctx;
  size_t column_count = zsv_column_count(data->parser);
  size_t nonblank = 0;
  for(size_t i = 0; i < column_count; i++)
    if(zsv_get_cell(data->parser, i).len)
      nonblank++;
  printf("Row %zu has %zu columns of which %zu are non-blank\n", data->rows, column_count, nonblank);
}

int main() {
  struct my_data data = { 0 };
  struct zsv_opts opts = { 0 };
  opts.row = my_row_handler;
  opts.ctx = &data;
  data.parser = zsv_new(&opts);
  while(zsv_parse_more(data.parser) == zsv_status_ok)
    ;
  zsv_finish(data.parser);
  zsv_delete(data.parser);
  return 0;
}
