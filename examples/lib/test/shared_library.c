#include <stdio.h>
#include <string.h>
#include <zsv.h>

int main(int argc, char **argv) {
  const char *expected[][2] = {{"name", "city"}, {"Ada", "London, UK"}};
  const size_t expected_rows = sizeof(expected) / sizeof(expected[0]);
  size_t row = 0;
  int result = 1;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <csv-file>\n", argv[0]);
    return 1;
  }
  FILE *input = fopen(argv[1], "rb");
  if (!input) {
    perror(argv[1]);
    return 1;
  }

  struct zsv_opts opts = {0};
  opts.stream = input;
  zsv_parser parser = zsv_new(&opts);
  if (!parser) {
    fprintf(stderr, "Could not create parser\n");
    fclose(input);
    return 1;
  }

  enum zsv_status status;
  while ((status = zsv_next_row(parser)) == zsv_status_row) {
    if (row >= expected_rows || zsv_cell_count(parser) != 2) {
      fprintf(stderr, "Unexpected row or field count at row %zu\n", row + 1);
      goto done;
    }
    for (size_t column = 0; column < 2; column++) {
      struct zsv_cell cell = zsv_get_cell(parser, column);
      const char *value = expected[row][column];
      if (cell.len != strlen(value) || memcmp(cell.str, value, cell.len)) {
        fprintf(stderr, "Unexpected value at row %zu, column %zu\n", row + 1, column + 1);
        goto done;
      }
    }
    printf("row %zu: %s | %s\n", row + 1, expected[row][0], expected[row][1]);
    row++;
  }
  if (status != zsv_status_done || row != expected_rows) {
    fprintf(stderr, "Expected %zu rows, got %zu (status %d)\n", expected_rows, row, (int)status);
    goto done;
  }
  printf("Shared library consumer passed: %zu rows, 2 fields each\n", row);
  result = 0;

done:
  zsv_delete(parser);
  fclose(input);
  return result;
}
