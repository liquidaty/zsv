/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

// --sample-size: see sample.h. Included from select.c after zsv_select_output_data_row()

#include <errno.h>

// statuses on which a parse stopped normally: the main loop in zsv_select_main() treats each
// of these as success, and the count pass and the reservoir flush must agree with it
static inline int zsv_select_parse_stopped_ok(enum zsv_status s) {
  return s == zsv_status_ok || s == zsv_status_no_more_input || s == zsv_status_cancelled ||
         s == zsv_status_nonstandard_csv
#ifdef ZSV_EXTRAS
         || s == zsv_status_max_rows_read
#endif
    ;
}

// parse an unsigned decimal integer: no sign, no whitespace, no trailing text, no overflow.
// returns 0 on success
static int zsv_select_sample_parse_u64(const char *s, uint64_t *out) {
  if (!s || *s < '0' || *s > '9')
    return 1;
  char *end;
  errno = 0;
  unsigned long long v = strtoull(s, &end, 10);
  if (errno == ERANGE || *end)
    return 1;
  *out = v;
  return 0;
}

// zsv_select_sample_arg(): --sample-size <n> or --seed <n>
static enum zsv_status zsv_select_sample_arg(struct zsv_select_data *data, const char *arg, const char *v) {
  uint64_t n;
  char is_size = !strcmp(arg, "--sample-size");
  if (zsv_select_sample_parse_u64(v, &n) || (is_size && (n == 0 || n > SIZE_MAX)))
    return zsv_printerr(1, "%s: invalid value '%s' (expected a positive integer)", arg, v);
  if (is_size)
    data->sample.size = (size_t)n;
  else {
    data->sample.seed = n;
    data->sample.have_seed = 1;
  }
  return zsv_status_ok;
}

// zsv_select_sample_init(): validate option combinations, seed the generator and pick
// the sampling strategy. Call after the input stream is open and before parsing
static enum zsv_status zsv_select_sample_init(struct zsv_select_data *data) {
  struct zsv_select_sample *s = &data->sample;
  if (s->size && (data->sample_every_n || data->sample_pct != 0))
    return zsv_printerr(1, "--sample-size cannot be used with --sample-every or --sample-pct");
#ifndef ZSV_NO_PARALLEL
  if (s->size && data->num_chunks > 1)
    return zsv_printerr(1, "--sample-size cannot be used with -j,--jobs or --parallel");
#endif
  if (s->have_seed && !s->size && data->sample_pct == 0)
    return zsv_printerr(1, "--seed requires --sample-size or --sample-pct");
  zsv_rng_seed(&s->rng, s->have_seed ? s->seed : zsv_rng_entropy());
  s->pct_threshold = data->sample_pct <= 0     ? 0
                     : data->sample_pct >= 100 ? 1000000
                                               : (uint64_t)(data->sample_pct * 10000.0);
  if (!s->size)
    return zsv_status_ok;

  // a second pass needs a rewindable stream
  s->reservoir = !zsv_file_is_regular(data->opts->stream);
#ifdef ZSV_EXTRAS
  // ... whose rows are not patched by an overwrite source: its iterator is forward-only and
  // shared with the main parser
  struct zsv_opt_overwrite no_overwrite = {0};
  if (data->opts->overwrite_auto || memcmp(&data->opts->overwrite, &no_overwrite, sizeof(no_overwrite)) != 0)
    s->reservoir = 1;
#endif
  return zsv_status_ok;
}

// zsv_select_sample_slot_new(): the next unused reservoir slot, growing the array on demand
// (doubling, capped at size) so a --sample-size far beyond the input costs nothing
static struct zsv_select_sample_slot *zsv_select_sample_slot_new(struct zsv_select_sample *s) {
  if (s->filled == s->slots_cap) {
    size_t cap = s->slots_cap ? s->slots_cap * 2 : 64;
    if (cap < s->slots_cap || cap > s->size)
      cap = s->size;
    if (cap > SIZE_MAX / sizeof(*s->slots))
      return NULL;
    struct zsv_select_sample_slot *slots = realloc(s->slots, cap * sizeof(*slots));
    if (!slots)
      return NULL;
    memset(slots + s->slots_cap, 0, (cap - s->slots_cap) * sizeof(*slots));
    s->slots = slots;
    s->slots_cap = cap;
  }
  return &s->slots[s->filled++];
}

/* ---- count pass (rewindable input) ---- */

// after the header: count rows only (no cell storage) unless search terms need cell contents
static void zsv_select_sample_count_row(void *ctx) {
  struct zsv_select_data *clone = ctx;
  if (UNLIKELY(zsv_cell_count(clone->parser) == 0 || clone->cancelled))
    return;
  if (zsv_select_row_in_population(clone))
    clone->sample.pop_remaining++;
  zsv_select_row_limit(clone);
}

static void zsv_select_sample_count_row_fast(void *ctx) {
  struct zsv_select_data *clone = ctx;
  if (UNLIKELY(clone->cancelled)) // -H: rows already buffered by the parser keep arriving
    return;
  if (zsv_select_row_in_population(clone)) // no cell access: search_hit() is 1 without search terms
    clone->sample.pop_remaining++;
  zsv_select_row_limit(clone);
}

static void zsv_select_sample_count_header(void *ctx) {
  struct zsv_select_data *clone = ctx;
  if (clone->search_strings
#ifdef HAVE_PCRE2_8
      || clone->search_regexs
#endif
  )
    zsv_set_row_handler(clone->parser, zsv_select_sample_count_row);
  else {
    zsv_set_skip_cells(clone->parser, 1);
    zsv_set_row_handler(clone->parser, zsv_select_sample_count_row_fast);
  }
}

// zsv_select_sample_count(): count the population with a throwaway parser over the same
// stream, from the position the input started at, then restore the current position. The
// caller has already consumed `consumed` bytes (the --fixed-auto preview) that it will replay.
// Call once the main parser is fully configured, so the clone enumerates the same rows
static enum zsv_status zsv_select_sample_count(struct zsv_select_data *data, size_t consumed) {
  enum zsv_status stat = zsv_status_ok;
  struct zsv_select_data *clone = NULL;
  zsv_parser p = NULL;
  FILE *stream = data->opts->stream;

  off_t base = ftello(stream);
  if (base < (off_t)consumed || fseeko(stream, base - (off_t)consumed, SEEK_SET) != 0) {
    stat = zsv_printerr(1, "Unable to rewind input for --sample-size");
    goto done;
  }
  if (!(clone = malloc(sizeof(*clone)))) { // heap: the struct exceeds the stack-buffer budget
    stat = zsv_status_memory;
    goto done;
  }
  memcpy(clone, data, sizeof(*clone));
  clone->sample.pop_remaining = 0;
  clone->verbose = 0;

  struct zsv_opts opts = *data->opts;
  opts.row_handler = zsv_select_sample_count_header;
  opts.ctx = clone;
  opts.errprintf = zsv_no_printf; // malformed-input diagnostics are reported once, by the main pass
#ifdef ZSV_EXTRAS
  opts.progress.callback = NULL;
  opts.completed.callback = NULL;
#endif
  if (!(p = zsv_new(&opts))) {
    stat = zsv_status_memory;
    goto done;
  }
  clone->parser = p;
  if (data->fixed.count && zsv_set_fixed_offsets(p, data->fixed.count, data->fixed.offsets) != zsv_status_ok) {
    stat = zsv_status_error;
    goto done;
  }

  enum zsv_status p_stat = zsv_status_ok;
  while (p_stat == zsv_status_ok && !zsv_signal_interrupted && !clone->cancelled)
    p_stat = zsv_parse_more(p);
  if (p_stat == zsv_status_no_more_input)
    zsv_finish(p);
  else if (!zsv_select_parse_stopped_ok(p_stat)) {
    stat = p_stat;
    goto done;
  }

  data->sample.pop_remaining = clone->sample.pop_remaining;
  data->sample.want_remaining =
    data->sample.size < clone->sample.pop_remaining ? data->sample.size : clone->sample.pop_remaining;

done:
  if (p)
    zsv_delete(p);
  free(clone);
  if (base >= 0 && fseeko(stream, base, SEEK_SET) != 0 && stat == zsv_status_ok)
    stat = zsv_printerr(1, "Unable to rewind input for --sample-size");
  return stat;
}

/* ---- reservoir (non-rewindable input) ---- */

// copy this row's projected cells into `slot`. On allocation failure the row is dropped
// (slot->len == 0) and the parse is cancelled with sample.failed set
static void zsv_select_sample_store(struct zsv_select_data *data, struct zsv_select_sample_slot *slot) {
  slot->row_ix = data->data_row_count;
  slot->len = 0;
  for (unsigned int i = 0; i < data->output_cols_count; i++) {
    // cell.str points into the parser's row buffer and is invalidated when the next cell is
    // cleaned, so it is copied out before the loop advances
    struct zsv_cell cell = zsv_select_output_cell(data, i);
    size_t need = sizeof(size_t) + 1 + cell.len;
    if (need < cell.len || slot->len > SIZE_MAX - need) // overflow
      goto fail;
    if (slot->len + need > slot->cap) {
      size_t cap = slot->cap < 64 ? 64 : slot->cap;
      while (cap < slot->len + need) {
        if (cap > SIZE_MAX / 2)
          goto fail;
        cap *= 2;
      }
      unsigned char *buf = realloc(slot->buf, cap);
      if (!buf)
        goto fail;
      slot->buf = buf;
      slot->cap = cap;
    }
    unsigned char *dst = slot->buf + slot->len;
    memcpy(dst, &cell.len, sizeof(size_t));
    dst[sizeof(size_t)] = (unsigned char)cell.quoted;
    if (cell.len) // cell.str is NULL for a column past the end of a short row
      memcpy(dst + sizeof(size_t) + 1, cell.str, cell.len);
    slot->len += need;
  }
  return;

fail:
  slot->len = 0;
  data->sample.failed = 1;
  data->cancelled = 1;
}

static int zsv_select_sample_slot_cmp(const void *a, const void *b) {
  size_t x = ((const struct zsv_select_sample_slot *)a)->row_ix;
  size_t y = ((const struct zsv_select_sample_slot *)b)->row_ix;
  return x < y ? -1 : x > y;
}

// zsv_select_sample_flush(): emit the reservoir in input order. Call only after a complete
// parse: a partial reservoir is not a uniform sample of the input
static enum zsv_status zsv_select_sample_flush(struct zsv_select_data *data) {
  struct zsv_select_sample *s = &data->sample;
  qsort(s->slots, s->filled, sizeof(*s->slots), zsv_select_sample_slot_cmp);
  for (size_t i = 0; i < s->filled; i++) {
    struct zsv_select_sample_slot *slot = &s->slots[i];
    char first = 1;
    if (data->prepend_line_number) {
      zsv_writer_cell_zu(data->csv_writer, first, slot->row_ix);
      first = 0;
    }
    for (size_t off = 0; off < slot->len; first = 0) {
      size_t len;
      memcpy(&len, slot->buf + off, sizeof(size_t));
      char quoted = (char)slot->buf[off + sizeof(size_t)];
      if (zsv_writer_cell(data->csv_writer, first, slot->buf + off + sizeof(size_t) + 1, len, quoted) !=
          zsv_writer_status_ok)
        return zsv_status_error;
      off += sizeof(size_t) + 1 + len;
    }
  }
  return zsv_status_ok;
}

// idempotent
static void zsv_select_sample_free(struct zsv_select_sample *s) {
  for (size_t i = 0; i < s->filled; i++)
    free(s->slots[i].buf);
  free(s->slots);
  s->slots = NULL;
  s->slots_cap = s->filled = s->seen = 0;
}

/* ---- row handler ---- */

// zsv_select_data_row_sample(): --sample-size replacement for zsv_select_data_row()
static void zsv_select_data_row_sample(void *ctx) {
  struct zsv_select_data *data = ctx;
  if (UNLIKELY(zsv_cell_count(data->parser) == 0 || data->cancelled))
    return;

  if (zsv_select_row_in_population(data)) {
    struct zsv_select_sample *s = &data->sample;
    if (s->reservoir) { // Algorithm R
      struct zsv_select_sample_slot *slot = NULL;
      s->seen++;
      if (s->seen <= s->size) {
        if (!(slot = zsv_select_sample_slot_new(s))) {
          s->failed = 1;
          data->cancelled = 1;
          return;
        }
      } else {
        uint64_t j = zsv_rng_below(&s->rng, (uint64_t)s->seen);
        if (j < (uint64_t)s->size)
          slot = &s->slots[j];
      }
      if (slot)
        zsv_select_sample_store(data, slot);
    } else if (s->want_remaining) { // Algorithm S; guarded so a count/main mismatch cannot underflow
      uint64_t u = s->pop_remaining ? zsv_rng_below(&s->rng, (uint64_t)s->pop_remaining) : 0;
      if (s->pop_remaining)
        s->pop_remaining--;
      if (u < (uint64_t)s->want_remaining) {
        s->want_remaining--;
        zsv_select_output_data_row(data);
      }
    }
  }
  zsv_select_row_limit(data);
}
