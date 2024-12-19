#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include "index.h"

struct zsv_index *zsv_index_new(void) {
  struct zsv_index *ix = calloc(1, sizeof(*ix));

  if (!ix)
    return ix;

  const size_t init_cap = 512;
  ix->first = calloc(1, sizeof(*ix->first) + init_cap * sizeof(ix->first->u64s[0]));
  ix->first->capacity = init_cap;

  return ix;
}

void zsv_index_delete(struct zsv_index *ix) {
  if (ix) {
    struct zsv_index_array *arr = ix->first;

    while (arr) {
      struct zsv_index_array *a = arr;
      arr = arr->next;
      free(a);
    }

    free(ix);
  }
}

enum zsv_index_status zsv_index_add_row(struct zsv_index *ix, uint64_t line_end) {
  struct zsv_index_array *arr = ix->first;
  size_t len = arr->len, cap = arr->capacity;

  if (!ix->header_line_end) {
    ix->header_line_end = line_end;
    return zsv_index_status_ok;
  }

  ix->row_count_local++;

  if ((ix->row_count_local & (ZSV_INDEX_ROW_N - 1)) != 0)
    return zsv_index_status_ok;

  while (len >= cap) {
    assert(len == cap);

    if (!arr->next) {
      len = 0;
      cap *= 2;
      arr->next = calloc(1, sizeof(*arr) + cap * sizeof(arr->u64s[0]));
      arr = arr->next;
      if (!arr)
        return zsv_index_status_memory;
      arr->capacity = cap;
    } else {
      arr = arr->next;
      len = arr->len;
      cap = arr->capacity;
    }
  }

  arr->u64s[len] = line_end;
  arr->len++;

  return zsv_index_status_ok;
}

void zsv_index_commit_rows(struct zsv_index *ix) {
  ix->row_count = ix->row_count_local;
}

enum zsv_index_status zsv_index_row_end_offset(const struct zsv_index *ix, uint64_t row, uint64_t *offset_out,
                                               uint64_t *remaining_rows_out) {
  assert(ix->row_count <= ix->row_count_local);

  if (row > ix->row_count)
    return zsv_index_status_error;

  if (row < ZSV_INDEX_ROW_N) {
    *offset_out = ix->header_line_end;
    *remaining_rows_out = row;

    return zsv_index_status_ok;
  }

  const size_t i = (row >> ZSV_INDEX_ROW_SHIFT) - 1;
  struct zsv_index_array *arr = ix->first;
  size_t lens = 0;

  while (i >= lens + arr->len) {
    assert(arr->next);

    lens += arr->len;
    arr = arr->next;
  }

  *offset_out = (long)arr->u64s[i - lens];
  *remaining_rows_out = row & (ZSV_INDEX_ROW_N - 1);

  return zsv_index_status_ok;
}

struct seek_row_ctx {
  uint64_t remaining_rows;
  zsv_parser parser;
};

static void seek_row_handler(void *ctx) {
  struct seek_row_ctx *c = ctx;

  c->remaining_rows--;
  if (c->remaining_rows > 0)
    return;

  zsv_abort(c->parser);
}

static enum zsv_index_status seek_and_check_newline(long *offset, struct zsv_opts *opts) {
  char new_line[2];
  zsv_generic_read read = (zsv_generic_read)fread;
  zsv_generic_seek seek = (zsv_generic_seek)fseek;
  FILE *stream = opts->stream;

  if (opts->seek)
    seek = opts->seek;

  if (opts->read)
    read = opts->read;

  if (seek(stream, *offset, SEEK_SET))
    return zsv_index_status_error;

  size_t nmemb = read(new_line, 1, 2, stream);

  if (nmemb < 1)
    return zsv_index_status_error;

  if (new_line[0] == '\n') {
    *offset += 1;
  } else if (new_line[0] == '\r') {
    *offset += 1;

    if (new_line[1] == '\n')
      *offset += 1;
  } else {
    return zsv_index_status_error;
  }

  if (seek(stream, *offset, SEEK_SET))
    return zsv_index_status_error;

  return zsv_index_status_ok;
}

enum zsv_index_status zsv_index_seek_row(const struct zsv_index *ix, struct zsv_opts *opts, uint64_t row) {
  uint64_t offset;
  uint64_t remaining_rows;
  enum zsv_index_status zist = zsv_index_row_end_offset(ix, row, &offset, &remaining_rows);

  if (zist != zsv_index_status_ok)
    return zist;

  if ((zist = seek_and_check_newline((long *)&offset, opts)) != zsv_index_status_ok)
    return zist;

  if (!remaining_rows)
    return zsv_index_status_ok;

  struct seek_row_ctx ctx = {
    .remaining_rows = remaining_rows,
  };
  struct zsv_opts o;
  memcpy(&o, opts, sizeof(o));
  o.ctx = &ctx;
  o.row_handler = seek_row_handler;
  zsv_parser parser = zsv_new(&o);
  ctx.parser = parser;

  enum zsv_status zst;
  while ((zst = zsv_parse_more(parser)) == zsv_status_ok)
    ;

  if (zst != zsv_status_cancelled)
    return zsv_index_status_error;

  offset += zsv_cum_scanned_length(parser);

  zsv_delete(parser);

  return seek_and_check_newline((long *)&offset, opts);
}
