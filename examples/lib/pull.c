#include <stdio.h>
#include <string.h>
#include <zsv.h>

/**
 * Simple example using libzsv as a pull parser
 * to call a row handler after each row is parsed
 *
 * This is the same as simple.c, but uses pull parsing instead of push parsing
 *
 * We will check each cell in the row to determine if it is blank, and output
 * the row number, the total number of cells and the number of blank cells
 *
 * Example:
 *   `echo 'abc,def\nghi,,,' | build/simple -`
 * Outputs:
 *   Row 1 has 2 columns of which 0 are non-blank
 *   Row 2 has 4 columns of which 3 are non-blank
 *
 */

/**
 * With pull parsing, the parser will not call our cell or row handlers
 * Instead, we use zsv_next_row() and then process the rows iteratively
 */

void my_row_handler(zsv_parser p, size_t row_num) {
  /* get a cell count */
  size_t cell_count = zsv_cell_count(p);

  /* iterate through each cell in this row, to count blanks */
  size_t nonblank = 0;
  for(size_t i = 0; i < cell_count; i++) {
    struct zsv_cell c = zsv_get_cell(p, i);
    /* use r.values[] and r.lengths[] to get cell data */
    /* Here, we only care about lengths */
    if(c.len > 0)
      nonblank++;
  }

  /* print our results for this row */
  printf("Row %zu has %zu columns of which %zu %s non-blank\n", row_num,
         cell_count, nonblank, nonblank == 1 ? "is" : "are");
}

/**
 * Main routine. Our program will take a single argument (a file name, or -)
 * and output, for each row, the numbers of total and blank cells
 */
int main(int argc, const char *argv[]) {

  /**
   * Process our arguments; output usage and/or errors if appropriate
   */
  if(argc != 2) {
    fprintf(stderr, "Reads a CSV file or stdin, and for each row,\n"
            " output counts of total and blank cells\n");
    fprintf(stderr, "Usage: simple <filename or dash(-) for stdin>\n");
    fprintf(stderr, "Example:\n"
            "  echo \"A1,B1,C1\\nA2,B2,\\nA3,,C3\\n,,C3\" | %s -\n\n", argv[0]);
    return 0;
  }

  FILE *f = strcmp(argv[1], "-") ? fopen(argv[1], "rb") : stdin;
  if(!f) {
    perror(argv[1]);
    return 1;
  }

  /**
   * Create a parser
   */
  struct zsv_opts opts = { 0 };
  opts.stream = f;
  zsv_parser parser = zsv_new(&opts);
  if(!parser) {
    fprintf(stderr, "Could not allocate parser!\n");
  }

  /* iterate through all rows */
  size_t row_num = 0;
  while(zsv_next_row(parser) == zsv_status_row)
    my_row_handler(parser, ++row_num);

  /**
   * Clean up
   */
  zsv_delete(parser);

  if(f != stdin)
    fclose(f);

  return 0;
}
