/**
 * Get a list of ending positions for each column name based on the ending position of each column name
 * where the first row is of the below form (dash = whitespace):
 * ----COLUMN1----COLUMN2-----COLUMN3----
 *
 * Approach:
 * - read the first [256k] of data [to do: alternatively, read only the first line]
 * - merge all read lines into a single line where line[i] = 'x' for each position i at which
 *   a non-white char appeared in any scanned line
 * - from the merged line, find each instance of white followed by not-white,
 *   but ignore the first instance of it
 */
static enum zsv_status auto_detect_fixed_column_sizes(struct fixed *fixed, struct zsv_opts *opts, unsigned char *buff,
                                                      size_t buffsize, size_t *buff_used, char verbose) {
  size_t max_lines = fixed->max_lines ? fixed->max_lines : 9999999999;
  enum zsv_status stat = zsv_status_ok;

  fixed->count = 0;
  char *line = calloc(buffsize, sizeof(*buff));
  if (!line) {
    stat = zsv_status_memory;
    goto auto_detect_fixed_column_sizes_exit;
  }
  memset(line, ' ', buffsize);

  *buff_used = fread(buff, 1, buffsize, opts->stream);
  if (!*buff_used) {
    stat = zsv_status_no_more_input;
    goto auto_detect_fixed_column_sizes_exit;
  }

  size_t lines_read = 0;
  size_t line_end = 0;
  size_t line_cursor = 0;
  char first = 1;
  char was_space = 1;
  char was_line_end = 0;
  for (size_t i = 0; i < *buff_used && (!line_end || max_lines == 0 || lines_read < max_lines);
       i++, line_cursor = was_line_end ? 0 : line_cursor + 1) {
    was_line_end = 0;
    // TO DO: support multi-byte unicode chars?
    switch (buff[i]) {
    case '\n':
    case '\r':
      if (line_cursor > line_end)
        line_end = line_cursor;
      was_line_end = 1;
      was_space = 1;
      lines_read++;
      break;
    case '\t':
    case '\v':
    case '\f':
    case ' ':
      was_space = 1;
      break;
    default:
      line[line_cursor] = 'x';
      if (was_space) {
        if (!line_end) { // only count columns for the first line
          if (first)
            first = 0;
          else
            fixed->count++;
        }
      }
      was_space = 0;
    }
  }
  if (!first)
    fixed->count++;

  if (!line_end) {
    stat = zsv_status_error;
    goto auto_detect_fixed_column_sizes_exit;
  }

  if (verbose)
    fprintf(stderr, "Calculating %zu columns from line:\n%.*s\n", fixed->count, (int)line_end, line);

  // allocate offsets
  free(fixed->offsets);
  fixed->offsets = NULL; // unnecessary line to silence codeQL false positive
  fixed->offsets = calloc(fixed->count, sizeof(*fixed->offsets));
  if (!fixed->offsets) {
    stat = zsv_status_memory;
    goto auto_detect_fixed_column_sizes_exit;
  }

  // now we have our merged line, so calculate the sizes
  // do the loop again, but assign values this time
  int count = 0;
  was_space = 1;
  first = 1;
  if (verbose)
    fprintf(stderr, "Running --fixed ");
  size_t i;
  for (i = 0; i < line_end; i++) {
    if (line[i] == 'x') {
      if (was_space) {
        if (first)
          first = 0;
        else {
          if (verbose)
            fprintf(stderr, "%s%zu", count ? "," : "", i);
          fixed->offsets[count++] = i;
        }
      }
      was_space = 0;
    } else
      was_space = 1;
  }
  if (!first) {
    if (verbose)
      fprintf(stderr, "%s%zu", count ? "," : "", i);
    if (i)
      fixed->offsets[count++] = i;
    fixed->count = count;
  }
  if (verbose)
    fprintf(stderr, "\n");

  // add a buffer to the last column in case subsequent lines are longer than what we scanned
  if (fixed->count)
    fixed->offsets[fixed->count - 1] += 50;

auto_detect_fixed_column_sizes_exit:
  free(line);
  return stat;
}
