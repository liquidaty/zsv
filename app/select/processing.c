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
