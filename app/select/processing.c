#ifndef NDEBUG
__attribute__((always_inline)) static inline
#endif
  unsigned char *
  zsv_select_cell_clean(struct zsv_select_data *data, unsigned char *utf8_value, char *quoted, size_t *lenp) {

  size_t len = *lenp;
  // to do: option to replace or warn non-printable chars 0 - 31:
  // vectorized scan
  // replace or warn if found
  if (UNLIKELY(data->unescape)) {
    size_t new_len = zsv_strunescape_backslash(utf8_value, len);
    if (new_len != len) {
      *quoted = 1;
      len = new_len;
    }
  }

  if (UNLIKELY(!data->no_trim_whitespace))
    utf8_value = (unsigned char *)zsv_strtrim(utf8_value, &len);

  if (UNLIKELY(data->clean_white))
    len = zsv_strwhite(utf8_value, len, data->whitespace_clean_flags); // to do: zsv_clean

  if (UNLIKELY(data->embedded_lineend && *quoted)) {
    unsigned char *tmp;
    const char *to_replace[] = {"\r\n", "\r", "\n"};
    for (int i = 0; i < 3; i++) {
      while ((tmp = memmem(utf8_value, len, to_replace[i], strlen(to_replace[i])))) {
        if (strlen(to_replace[i]) == 1)
          *tmp = data->embedded_lineend;
        else {
          size_t right_len = utf8_value + len - tmp;
          memmove(tmp + 1, tmp + 2, right_len - 2);
          *tmp = data->embedded_lineend;
          len--;
        }
      }
    }
    if (data->no_trim_whitespace)
      utf8_value = (unsigned char *)zsv_strtrim(utf8_value, &len);
  }
  *lenp = len;
  return utf8_value;
}

static inline char zsv_select_row_search_hit(struct zsv_select_data *data) {
  if (!data->search_strings
#ifdef HAVE_PCRE2_8
      && !data->search_regexs
#endif
  )
    return 1;

  char have_overwrite = 0;
  unsigned int j = zsv_cell_count(data->parser);
  // Convert all bytes between cells to NUL so we can accurately search the entire row in one goe
  unsigned char *start = NULL;
  unsigned char *end = NULL;
  for (unsigned int i = 0; i < j; i++) {
    struct zsv_cell cell = zsv_get_cell(data->parser, i);
    if (cell.overwritten)
      have_overwrite = 1;
    if (i == 0)
      start = cell.str;
    if (UNLIKELY(data->any_clean != 0))
      cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
    if (end) {
      while (end < cell.str) {
        *end = '\0';
        end++;
      }
    }
    end = cell.str + cell.len;
  }

  if (have_overwrite) {
    for (unsigned int i = 0; i < j; i++) {
      struct zsv_cell cell = zsv_get_cell(data->parser, i);
      if (cell.len) {
        start = cell.str;
        end = cell.str + cell.len;
        for (struct zsv_select_search_str *ss = data->search_strings; ss; ss = ss->next)
          if (ss->value && *ss->value && end > start && memmem(start, end - start, ss->value, ss->len))
            return 1;
#ifdef HAVE_PCRE2_8
        for (struct zsv_select_regex *rs = data->search_regexs; rs; rs = rs->next)
          if (rs->regex && zsv_pcre2_8_match(rs->regex, start, end - start))
            return 1;
#endif
      }
    }
  } else {
    if (end > start) {
      for (struct zsv_select_search_str *ss = data->search_strings; ss; ss = ss->next)
        if (ss->value && *ss->value && end > start && memmem(start, end - start, ss->value, ss->len))
          return 1;

#ifdef HAVE_PCRE2_8
      for (struct zsv_select_regex *rs = data->search_regexs; rs; rs = rs->next)
        if (rs->regex && zsv_pcre2_8_match(rs->regex, start, end - start))
          return 1;
#endif
    }
  }
  return 0;
}

// zsv_select_output_cell(): the i-th output cell after cleaning and --merge fallback.
// The returned bytes live in the parser's row buffer and stay valid only until the
// next cell is cleaned, so callers that retain them must copy before advancing
static inline struct zsv_cell zsv_select_output_cell(struct zsv_select_data *data, unsigned int i) {
  struct zsv_cell cell = zsv_get_cell(data->parser, data->out2in[i].ix);
  if (UNLIKELY(data->any_clean != 0)) {
    // leading/trailing white may have been converted to NULL for regex search
    while (cell.len && *cell.str == '\0')
      cell.str++, cell.len--;
    while (cell.len && cell.str[cell.len - 1] == '\0')
      cell.len--;
    cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
  }
  if (VERY_UNLIKELY(data->distinct == ZSV_SELECT_DISTINCT_MERGE) && UNLIKELY(cell.len == 0)) {
    for (struct zsv_select_uint_list *ix = data->out2in[i].merge.indexes; ix; ix = ix->next) {
      cell = zsv_get_cell(data->parser, ix->value);
      if (cell.len) {
        if (UNLIKELY(data->any_clean != 0))
          cell.str = zsv_select_cell_clean(data, cell.str, &cell.quoted, &cell.len);
        if (cell.len)
          break;
      }
    }
  }
  return cell;
}

// zsv_select_row_in_population(): advance per-row state (row count, -D countdown) and
// report whether this data row is in the population select operates on, i.e. it survived
// -D and matched -s/--regex-search. Shared by the plain, sampling and counting row
// handlers so they cannot enumerate different row sets
static inline char zsv_select_row_in_population(struct zsv_select_data *data) {
  data->data_row_count++;
  if (UNLIKELY(data->skip_data_rows)) {
    data->skip_data_rows--;
    return 0;
  }
  return zsv_select_row_search_hit(data);
}

// zsv_select_row_limit(): apply -H after each data row, whether or not it was output
static inline void zsv_select_row_limit(struct zsv_select_data *data) {
  if (UNLIKELY(data->data_rows_limit > 0) && data->data_row_count + 1 >= data->data_rows_limit)
    data->cancelled = 1;
  if (UNLIKELY(data->verbose) && data->data_row_count % 25000 == 0)
    fprintf(stderr, "Processed %zu rows\n", data->data_row_count);
}
