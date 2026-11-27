#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#elif defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zsv.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

static int loaded_from(const char *installed_library) {
#ifdef _WIN32
  return GetModuleHandleA(installed_library) != NULL;
#else
  Dl_info info;
  char *loaded = dladdr((const void *)zsv_new, &info) ? realpath(info.dli_fname, NULL) : NULL;
  char *installed = realpath(installed_library, NULL);
  int matches = loaded && installed && strcmp(loaded, installed) == 0;
  free(loaded);
  free(installed);
  return matches;
#endif
}

int main(int argc, char **argv) {
  const char *expected[][2] = {{"name", "city"}, {"Ada", "London, UK"}};
  const size_t expected_rows = sizeof(expected) / sizeof(expected[0]);
  size_t row = 0;
  int result = 1;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <csv-file> <installed-library>\n", argv[0]);
    return 1;
  }
  if (!loaded_from(argv[2])) {
    fprintf(stderr, "Expected installed shared library was not loaded: %s\n", argv[2]);
    return 1;
  }
  printf("Loaded installed shared library: %s\n", argv[2]);

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
