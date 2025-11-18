static enum zsv_select_column_index_selection_type zsv_select_column_index_selection(const unsigned char *arg,
                                                                                     unsigned *lo, unsigned *hi);

static inline void zsv_select_add_exclusion(struct zsv_select_data *data, const char *name) {
  if (data->exclusion_count < MAX_EXCLUSIONS)
    data->exclusions[data->exclusion_count++] = (const unsigned char *)name;
}

static inline unsigned char *zsv_select_get_header_name(struct zsv_select_data *data, unsigned in_ix) {
  if (in_ix < data->header_name_count)
    return data->header_names[in_ix];
  return NULL;
}

static inline char zsv_select_excluded_current_header_name(struct zsv_select_data *data, unsigned in_ix) {
  if (data->exclusion_count) {
    if (data->use_header_indexes) {
      for (unsigned int ix = 0; ix < data->exclusion_count; ix++) {
        unsigned i, j;
        switch (zsv_select_column_index_selection(data->exclusions[ix], &i, &j)) {
        case zsv_select_column_index_selection_type_none:
          // not expected!
          break;
        case zsv_select_column_index_selection_type_single:
          if (in_ix + 1 == i)
            return 1;
          break;
        case zsv_select_column_index_selection_type_range:
          if (i <= in_ix + 1 && in_ix + 1 <= j)
            return 1;
          break;
        case zsv_select_column_index_selection_type_lower_bounded:
          if (i <= in_ix + 1)
            return 1;
          break;
        }
      }
    } else {
      unsigned char *header_name = zsv_select_get_header_name(data, in_ix);
      if (header_name) {
        for (unsigned int i = 0; i < data->exclusion_count; i++)
          if (!zsv_stricmp(header_name, data->exclusions[i]))
            return 1;
      }
    }
  }
  return 0;
}

// zsv_select_find_header(): return 1-based index, or 0 if not found
static int zsv_select_find_header(struct zsv_select_data *data, const unsigned char *header_name) {
  if (header_name) {
    for (unsigned int i = 0; i < data->output_cols_count; i++) {
      unsigned char *prior_header_name = zsv_select_get_header_name(data, data->out2in[i].ix);
      if (prior_header_name && !zsv_stricmp(header_name, prior_header_name))
        return i + 1;
    }
  }
  return 0;
}

static int zsv_select_add_output_col(struct zsv_select_data *data, unsigned in_ix) {
  int err = 0;
  if (data->output_cols_count < data->opts->max_columns) {
    int found = zsv_select_find_header(data, zsv_select_get_header_name(data, in_ix));
    if (data->distinct && found) {
      if (data->distinct == ZSV_SELECT_DISTINCT_MERGE) {
        // add this index
        struct zsv_select_uint_list *ix = calloc(1, sizeof(*ix));
        if (!ix)
          err = zsv_printerr(1, "Out of memory!\n");
        else {
          ix->value = in_ix;
          if (!data->out2in[found - 1].merge.indexes)
            data->out2in[found - 1].merge.indexes = ix;
          else
            *data->out2in[found - 1].merge.last_index = ix;
          data->out2in[found - 1].merge.last_index = &ix->next;
        }
      }
      return err;
    }
    if (zsv_select_excluded_current_header_name(data, in_ix))
      return err;
    data->out2in[data->output_cols_count++].ix = in_ix;
  }
  return err;
}

// not very fast, but we don't need it to be
static inline unsigned int str_array_ifind(const unsigned char *needle, unsigned char *haystack[], unsigned hay_count) {
  for (unsigned int i = 0; i < hay_count; i++) {
    if (!(needle && *needle) && !(haystack[i] && *haystack[i]))
      return i + 1;
    if (!(needle && *needle && haystack[i] && *haystack[i]))
      continue;
    if (!zsv_stricmp(needle, haystack[i]))
      return i + 1;
  }
  return 0;
}

static int zsv_select_set_output_columns(struct zsv_select_data *data) {
  int err = 0;
  unsigned int header_name_count = data->header_name_count;
  if (!data->col_argc) {
    for (unsigned int i = 0; !err && i < header_name_count; i++)
      err = zsv_select_add_output_col(data, i);
  } else if (data->use_header_indexes) {
    for (int arg_i = 0; !err && arg_i < data->col_argc; arg_i++) {
      const char *arg = data->col_argv[arg_i];
      unsigned i, j;
      switch (zsv_select_column_index_selection((const unsigned char *)arg, &i, &j)) {
      case zsv_select_column_index_selection_type_none:
        zsv_printerr(1, "Invalid column index: %s", arg);
        err = -1;
        break;
      case zsv_select_column_index_selection_type_single:
        err = zsv_select_add_output_col(data, i - 1);
        break;
      case zsv_select_column_index_selection_type_range:
        while (i <= j && i < data->opts->max_columns) {
          err = zsv_select_add_output_col(data, i - 1);
          i++;
        }
        break;
      case zsv_select_column_index_selection_type_lower_bounded:
        if (i) {
          for (unsigned int k = i - 1; !err && k < header_name_count; k++)
            err = zsv_select_add_output_col(data, k);
        }
        break;
      }
    }
  } else { // using header names
    for (int arg_i = 0; !err && arg_i < data->col_argc; arg_i++) {
      // find the location of the matching header name, if any
      unsigned int in_pos =
        str_array_ifind((const unsigned char *)data->col_argv[arg_i], data->header_names, header_name_count);
      if (!in_pos) {
        fprintf(stderr, "Column %s not found\n", data->col_argv[arg_i]);
        err = -1;
      } else
        err = zsv_select_add_output_col(data, in_pos - 1);
    }
  }
  return err;
}

static enum zsv_select_column_index_selection_type zsv_select_column_index_selection(const unsigned char *arg,
                                                                                     unsigned *lo, unsigned *hi) {
  enum zsv_select_column_index_selection_type result = zsv_select_column_index_selection_type_none;

  unsigned int i = 0;
  unsigned int j = 0;
  int n = 0;
  int k = sscanf((const char *)arg, "%u-%u%n", &i, &j, &n);
  if (k == 2) {
    if (n >= 0 && (size_t)n == strlen((const char *)arg) && i > 0 && j >= i)
      result = zsv_select_column_index_selection_type_range;
  } else {
    k = sscanf((const char *)arg, "%u%n", &i, &n);
    if (k == 1 && n >= 0 && (size_t)n == strlen((const char *)arg)) {
      if (i > 0)
        result = zsv_select_column_index_selection_type_single;
    } else {
      k = sscanf((const char *)arg, "%u-%n", &i, &n);
      if (k == 1 && n >= 0 && (size_t)n == strlen((const char *)arg)) {
        if (i > 0) {
          result = zsv_select_column_index_selection_type_lower_bounded;
          j = 0;
        }
      }
    }
  }
  if (lo)
    *lo = i;
  if (hi)
    *hi = j;
  return result;
}

// zsv_select_check_exclusions_are_indexes(): return err
static int zsv_select_check_exclusions_are_indexes(struct zsv_select_data *data) {
  int err = 0;
  for (unsigned int e = 0; e < data->exclusion_count; e++) {
    const unsigned char *arg = data->exclusions[e];
    if (zsv_select_column_index_selection(arg, NULL, NULL) == zsv_select_column_index_selection_type_none)
      err = zsv_printerr(1, "Invalid column index: %s", arg);
  }
  return err;
}
