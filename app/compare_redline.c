/*
 * Copyright (C) 2023 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

/*
 * Redline-mode JSON output for `zsv compare --json-redline`.
 *
 * This file is #included directly into compare.c (a single translation unit), so its
 * helpers are static and rely on the headers and declarations already pulled in there
 * (notably zsv_compare_within_tolerance and the forward-declared zsv_compare_collect_row).
 * The data model lives in compare_redline.h.
 */

// Copy a cell value into *dest_s/*dest_len, which must start NULL/0 (caller-zeroed). Empty/missing cells
// are left NULL/0 (rendered as JSON null); *dest_len is only set on a successful copy, so it stays consistent
// with *dest_s on every path. Returns nonzero only on allocation failure (callers must check — warn_unused_result).
static ZSV_WARN_UNUSED_RESULT int zsv_compare_redline_dup_cell(unsigned char **dest_s, size_t *dest_len,
                                                               struct zsv_cell v) {
  if (v.str && v.len) {
    if (!(*dest_s = zsv_memdup(v.str, v.len)))
      return 1;
    *dest_len = v.len;
  }
  return 0;
}

// True if output column j is present in every input
static int zsv_compare_col_in_all(struct zsv_compare_data *data, unsigned j) {
  for (unsigned i = 0; i < data->input_count; i++)
    if (data->inputs[i].out2in[j] == 0)
      return 0;
  return 1;
}

// Fill cell with the scalar from the first present (sorted) input that has output column j.
// Returns nonzero only on allocation failure.
static int zsv_compare_redline_scalar_from_first(struct zsv_compare_data *data, struct zsv_compare_redline_cell *cell,
                                                 unsigned last_ix, unsigned j) {
  for (unsigned pi = 0; pi <= last_ix; pi++) {
    struct zsv_compare_input *inp = data->inputs_to_sort[pi];
    if (inp->out2in[j] != 0)
      return zsv_compare_redline_dup_cell(&cell->scalar_s, &cell->scalar_len, data->get_cell(inp, inp->out2in[j] - 1));
  }
  return 0;
}

// Emit s as a JSON string, or null when s is NULL
static void zsv_compare_redline_emit_scalar(jsonwriter_handle jsw, const unsigned char *s, size_t len) {
  if (s)
    jsonwriter_strn(jsw, s, len);
  else
    jsonwriter_null(jsw);
}

// Free e's top-level arrays and e itself (rows are freed separately). Extend this when adding heap members.
static void zsv_compare_redline_free_base(struct zsv_compare_redline *e) {
  free(e->input_row_counts);
  free(e->rows_only_in_input);
  free(e->col_stats);
  free(e);
}

static struct zsv_compare_redline *zsv_compare_redline_new(unsigned input_count, unsigned output_colcount) {
  struct zsv_compare_redline *e = calloc(1, sizeof(*e));
  if (!e)
    return NULL;
  e->rows_tail = &e->rows_head;
  if (input_count > 0) {
    if (!(e->input_row_counts = calloc(input_count, sizeof(*e->input_row_counts))))
      goto err;
    if (!(e->rows_only_in_input = calloc(input_count, sizeof(*e->rows_only_in_input))))
      goto err;
  }
  if (output_colcount > 0) {
    if (!(e->col_stats = calloc(output_colcount, sizeof(*e->col_stats))))
      goto err;
  }
  return e;
err:
  zsv_compare_redline_free_base(e);
  return NULL;
}

static int zsv_compare_uint_cmp(const void *a, const void *b) {
  unsigned ua = *(const unsigned *)a;
  unsigned ub = *(const unsigned *)b;
  return (ua > ub) - (ua < ub);
}

static void zsv_compare_redline_cell_free(struct zsv_compare_redline_cell *cell, unsigned input_count) {
  free(cell->scalar_s);
  if (cell->diff_s) {
    for (unsigned i = 0; i < input_count; i++)
      free(cell->diff_s[i]);
    free(cell->diff_s);
  }
  free(cell->diff_len);
}

static void zsv_compare_redline_row_free(struct zsv_compare_redline_row *row, unsigned input_count,
                                         unsigned output_colcount) {
  if (!row)
    return;
  free(row->missing_in);
  if (row->cells) {
    for (unsigned j = 0; j < output_colcount; j++)
      zsv_compare_redline_cell_free(&row->cells[j], input_count);
    free(row->cells);
  }
  free(row);
}

static void zsv_compare_redline_free(struct zsv_compare_redline *e, unsigned input_count, unsigned output_colcount) {
  if (!e)
    return;
  struct zsv_compare_redline_row *row = e->rows_head;
  while (row) {
    struct zsv_compare_redline_row *next = row->next;
    zsv_compare_redline_row_free(row, input_count, output_colcount);
    row = next;
  }
  zsv_compare_redline_free_base(e);
}

static void zsv_compare_collect_row(struct zsv_compare_data *data, unsigned last_ix) {
  struct zsv_compare_redline *e = data->redline;
  if (!e)
    return;
  unsigned input_count = data->input_count;
  unsigned output_colcount = data->output_colcount;
  char row_in_all = (last_ix + 1 == input_count);
  unsigned missing_count = input_count - (last_ix + 1);

  /* Track per-input row counts */
  for (unsigned i = 0; i <= last_ix; i++)
    e->input_row_counts[data->inputs_to_sort[i]->index]++;

  if (row_in_all)
    e->rows_in_all++;
  else
    for (unsigned i = last_ix + 1; i < input_count; i++)
      e->rows_only_in_input[data->inputs_to_sort[i]->index]++;

  /* Allocate row */
  struct zsv_compare_redline_row *row = calloc(1, sizeof(*row));
  if (row)
    row->cells = calloc(output_colcount, sizeof(*row->cells));
  if (!row || !row->cells)
    goto oom;

  char has_diff = 0;

  if (!row_in_all) {
    /* Object form */
    row->is_object = 1;
    row->missing_in_count = missing_count;
    row->missing_in = malloc(missing_count * sizeof(*row->missing_in));
    if (!row->missing_in)
      goto oom;
    for (unsigned i = 0; i < missing_count; i++)
      row->missing_in[i] = data->inputs_to_sort[last_ix + 1 + i]->index;
    qsort(row->missing_in, missing_count, sizeof(*row->missing_in), zsv_compare_uint_cmp);
    has_diff = 1;

    /* Per-column: use value from first present input that has this column */
    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next)
      if (zsv_compare_redline_scalar_from_first(data, &row->cells[j], last_ix, j))
        goto oom;
  } else {
    /* Array form: all inputs present — compare cells */
    struct zsv_compare_input *inp0 = data->inputs_to_sort[0];

    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next) {
      if (!zsv_compare_col_in_all(data, j)) {
        /* Schema-diff column: scalar from first input that has it */
        if (zsv_compare_redline_scalar_from_first(data, &row->cells[j], last_ix, j))
          goto oom;
        continue; /* not counted in cells.compared */
      }

      /* Column is in all inputs — update stats */
      e->col_stats[j].compared++;
      e->cells_compared++;

      /* Get reference value from inputs_to_sort[0] */
      struct zsv_cell v0 = {0};
      if (inp0->out2in[j] != 0)
        v0 = data->get_cell(inp0, inp0->out2in[j] - 1);

      /* For key columns: always matched */
      if (oc->is_key) {
        e->col_stats[j].matched++;
        e->cells_matched++;
        if (zsv_compare_redline_dup_cell(&row->cells[j].scalar_s, &row->cells[j].scalar_len, v0))
          goto oom;
        continue;
      }

      /* Compare against all other present inputs */
      char different = 0;
      for (unsigned pi = 1; pi <= last_ix && !different; pi++) {
        struct zsv_compare_input *inp = data->inputs_to_sort[pi];
        unsigned ci = inp->out2in[j];
        if (ci == 0)
          continue;
        struct zsv_cell vi = data->get_cell(inp, ci - 1);
        if (data->cmp(data->cmp_ctx, v0, vi, data, ci - 1))
          different = 1;
      }

      if (!different) {
        e->col_stats[j].matched++;
        e->cells_matched++;
        if (zsv_compare_redline_dup_cell(&row->cells[j].scalar_s, &row->cells[j].scalar_len, v0))
          goto oom;
        continue;
      }

      /* Check tolerance: all differing pairs (0 vs i) must be within tolerance */
      char tolerated = 0;
      if (data->tolerance.value > 0) {
        tolerated = 1;
        for (unsigned pi = 1; pi <= last_ix && tolerated; pi++) {
          struct zsv_compare_input *inp = data->inputs_to_sort[pi];
          unsigned ci = inp->out2in[j];
          if (ci != 0 && !zsv_compare_within_tolerance(data, v0, data->get_cell(inp, ci - 1)))
            tolerated = 0;
        }
      }

      if (tolerated) {
        e->col_stats[j].within_tolerance++;
        e->cells_within_tolerance++;
        row->cells[j].is_tolerated = 1;
        if (!data->writer.include_tolerated) {
          /* Collapse to input[0]'s scalar */
          if (zsv_compare_redline_dup_cell(&row->cells[j].scalar_s, &row->cells[j].scalar_len, v0))
            goto oom;
          continue;
        }
        /* With include_tolerated: render as diff array — counts as a diff for row inclusion */
        has_diff = 1;
        /* Fall through to build diff array */
      } else {
        e->col_stats[j].differing++;
        e->cells_differing++;
        has_diff = 1;
      }

      /* Build diff array */
      row->cells[j].is_diff = 1;
      row->cells[j].diff_s = calloc(input_count, sizeof(*row->cells[j].diff_s));
      row->cells[j].diff_len = calloc(input_count, sizeof(*row->cells[j].diff_len));
      if (!row->cells[j].diff_s || !row->cells[j].diff_len)
        goto oom;
      for (unsigned pi = 0; pi <= last_ix; pi++) {
        struct zsv_compare_input *inp = data->inputs_to_sort[pi];
        unsigned ci = inp->out2in[j];
        unsigned inp_ix = inp->index;
        /* Missing inputs stay NULL → emitted as JSON null */
        if (ci != 0 && zsv_compare_redline_dup_cell(&row->cells[j].diff_s[inp_ix], &row->cells[j].diff_len[inp_ix],
                                                    data->get_cell(inp, ci - 1)))
          goto oom;
      }
    }
  }

  /* Store row only if it has a diff or include_unchanged_rows is set */
  if (!has_diff && !data->writer.include_unchanged_rows) {
    zsv_compare_redline_row_free(row, input_count, output_colcount);
    return;
  }

  if (has_diff)
    e->rows_with_diff++;

  row->next = NULL;
  *e->rows_tail = row;
  e->rows_tail = &row->next;
  return;

oom:
  zsv_compare_redline_row_free(row, input_count, output_colcount);
  data->status = zsv_compare_status_memory;
}

static void zsv_compare_emit_redline(struct zsv_compare_data *data) {
  struct zsv_compare_redline *e = data->redline;
  jsonwriter_handle jsw = data->writer.handle.jsw;
  unsigned input_count = data->input_count;
  unsigned output_colcount = data->output_colcount;

  jsonwriter_start_object(jsw);

  /* Identity */
  jsonwriter_object_str(jsw, "schema", (const unsigned char *)"zsv.compare");
  jsonwriter_object_str(jsw, "version", (const unsigned char *)ZSV_COMPARE_REDLINE_VERSION);

  {
    /* Honor SOURCE_DATE_EPOCH (reproducible-builds convention) for deterministic output; else wall clock. */
    const char *sde = getenv("SOURCE_DATE_EPOCH");
    time_t now;
    if (sde && *sde) {
      char *end;
      long long epoch = strtoll(sde, &end, 10);
      now = (*end || epoch < 0) ? time(NULL) : (time_t)epoch;
    } else
      now = time(NULL);
    struct tm *utc = gmtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", utc);
    jsonwriter_object_cstr(jsw, "generated_at", buf);
  }

  /* inputs[] */
  jsonwriter_object_array(jsw, "inputs");
  for (unsigned i = 0; i < input_count; i++) {
    struct zsv_compare_input *inp = &data->inputs[i];
    jsonwriter_start_object(jsw);
    const char *slash = strrchr(inp->path, '/');
    const char *label = slash ? slash + 1 : inp->path;
    jsonwriter_object_cstr(jsw, "label", label);
    jsonwriter_object_cstr(jsw, "path", inp->path);
    jsonwriter_object_size_t(jsw, "row_count", e->input_row_counts[i]);
    jsonwriter_end_object(jsw);
  }
  jsonwriter_end_array(jsw);

  /* keys[] */
  jsonwriter_object_array(jsw, "keys");
  for (struct zsv_compare_key *k = data->keys; k; k = k->next)
    jsonwriter_cstr(jsw, k->name);
  jsonwriter_end_array(jsw);

  /* options */
  jsonwriter_object_object(jsw, "options");
  if (data->tolerance.original > 0)
    jsonwriter_object_dbl(jsw, "tolerance", (long double)data->tolerance.original);
  else
    jsonwriter_object_null(jsw, "tolerance");
  jsonwriter_object_bool(jsw, "sort", data->sort);
  jsonwriter_object_bool(jsw, "include_unchanged_rows", data->writer.include_unchanged_rows);
  jsonwriter_object_bool(jsw, "include_tolerated", data->writer.include_tolerated);
  jsonwriter_object_bool(jsw, "require_all_inputs", data->require_all_inputs);
  jsonwriter_end_object(jsw);

  /* columns[] */
  jsonwriter_object_array(jsw, "columns");
  {
    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next) {
      jsonwriter_start_object(jsw);
      jsonwriter_object_strn(jsw, "name", oc->name, oc->name_len);
      jsonwriter_object_bool(jsw, "is_key", oc->is_key);
      jsonwriter_object_array(jsw, "in_inputs");
      for (unsigned i = 0; i < input_count; i++)
        if (data->inputs[i].out2in[j] != 0)
          jsonwriter_int(jsw, (jsw_int64)i);
      jsonwriter_end_array(jsw);
      jsonwriter_end_object(jsw);
    }
  }
  jsonwriter_end_array(jsw);

  /* summary */
  jsonwriter_object_object(jsw, "summary");

  jsonwriter_object_object(jsw, "rows");
  jsonwriter_object_size_t(jsw, "in_all_inputs", e->rows_in_all);
  jsonwriter_object_array(jsw, "only_in_input_count");
  for (unsigned i = 0; i < input_count; i++)
    jsonwriter_size_t(jsw, e->rows_only_in_input[i]);
  jsonwriter_end_array(jsw);
  jsonwriter_object_size_t(jsw, "with_any_diff", e->rows_with_diff);
  jsonwriter_end_object(jsw); /* rows */

  jsonwriter_object_object(jsw, "cells");
  jsonwriter_object_size_t(jsw, "compared", e->cells_compared);
  jsonwriter_object_size_t(jsw, "matched", e->cells_matched);
  jsonwriter_object_size_t(jsw, "within_tolerance", e->cells_within_tolerance);
  jsonwriter_object_size_t(jsw, "differing", e->cells_differing);
  jsonwriter_end_object(jsw); /* cells */

  jsonwriter_object_array(jsw, "by_column");
  {
    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next) {
      jsonwriter_start_object(jsw);
      jsonwriter_object_strn(jsw, "name", oc->name, oc->name_len);
      jsonwriter_object_size_t(jsw, "compared", e->col_stats[j].compared);
      jsonwriter_object_size_t(jsw, "matched", e->col_stats[j].matched);
      jsonwriter_object_size_t(jsw, "within_tolerance", e->col_stats[j].within_tolerance);
      jsonwriter_object_size_t(jsw, "differing", e->col_stats[j].differing);
      jsonwriter_end_object(jsw);
    }
  }
  jsonwriter_end_array(jsw);

  jsonwriter_object_object(jsw, "schema");
  jsonwriter_object_array(jsw, "common");
  {
    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next)
      if (zsv_compare_col_in_all(data, j))
        jsonwriter_strn(jsw, oc->name, oc->name_len);
  }
  jsonwriter_end_array(jsw);
  jsonwriter_object_array(jsw, "only_in_input");
  for (unsigned i = 0; i < input_count; i++) {
    jsonwriter_start_array(jsw);
    zsv_compare_unique_colname *oc = data->output_colnames_first;
    for (unsigned j = 0; j < output_colcount && oc; j++, oc = oc->next) {
      if (data->inputs[i].out2in[j] == 0)
        continue;
      char only_here = 1;
      for (unsigned k = 0; k < input_count && only_here; k++)
        if (k != i && data->inputs[k].out2in[j] != 0)
          only_here = 0;
      if (only_here)
        jsonwriter_strn(jsw, oc->name, oc->name_len);
    }
    jsonwriter_end_array(jsw);
  }
  jsonwriter_end_array(jsw);
  jsonwriter_end_object(jsw); /* schema */

  jsonwriter_end_object(jsw); /* summary */

  /* rows[] */
  jsonwriter_object_array(jsw, "rows");
  for (struct zsv_compare_redline_row *row = e->rows_head; row; row = row->next) {
    if (row->is_object) {
      jsonwriter_start_object(jsw);
      jsonwriter_object_array(jsw, "data");
      for (unsigned j = 0; j < output_colcount; j++)
        zsv_compare_redline_emit_scalar(jsw, row->cells[j].scalar_s, row->cells[j].scalar_len);
      jsonwriter_end_array(jsw);
      jsonwriter_object_array(jsw, "missing_in");
      for (unsigned m = 0; m < row->missing_in_count; m++)
        jsonwriter_int(jsw, (jsw_int64)row->missing_in[m]);
      jsonwriter_end_array(jsw);
      jsonwriter_end_object(jsw);
    } else {
      jsonwriter_start_array(jsw);
      for (unsigned j = 0; j < output_colcount; j++) {
        struct zsv_compare_redline_cell *cell = &row->cells[j];
        if (cell->is_diff) {
          jsonwriter_start_array(jsw);
          for (unsigned i = 0; i < input_count; i++)
            zsv_compare_redline_emit_scalar(jsw, cell->diff_s ? cell->diff_s[i] : NULL,
                                            cell->diff_len ? cell->diff_len[i] : 0);
          jsonwriter_end_array(jsw);
        } else {
          zsv_compare_redline_emit_scalar(jsw, cell->scalar_s, cell->scalar_len);
        }
      }
      jsonwriter_end_array(jsw);
    }
  }
  jsonwriter_end_array(jsw); /* rows */

  jsonwriter_end_object(jsw); /* top level */

  /* Mirror the canonical path's per-differing-cell tally into diff_count so --return-count yields the
     same exit code in redline mode (the redline struct is freed before that code is read). */
  data->diff_count = e->cells_differing > INT_MAX ? INT_MAX : (int)e->cells_differing;
}
