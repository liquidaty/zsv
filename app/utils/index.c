#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>
#include <zsv/utils/index.h>

struct zsv_index *zsv_index_new(void) {
  struct zsv_index *ix = malloc(sizeof(*ix));

  if (!ix)
    return ix;

  memset(ix, 0, sizeof(*ix));

  const size_t init_cap = 256;
  ix->array = malloc(sizeof(*ix->array) + init_cap * sizeof(ix->array->u64s[0]));
  ix->array->capacity = init_cap;
  ix->array->len = 0;

  return ix;
}

enum zsv_index_status zsv_index_add_row(struct zsv_index *ix, zsv_parser parser) {
  struct zsv_index_array *arr = ix->array;
  size_t len = arr->len, cap = arr->capacity;
  uint64_t line_end = zsv_cum_scanned_length(parser);

  if (!ix->header_line_end) {
    ix->header_line_end = line_end;
    return zsv_index_status_ok;
  }

  ix->row_count++;

  if ((ix->row_count & (ZSV_INDEX_ROW_N - 1)) != 0)
    return zsv_index_status_ok;

  if (len >= cap) {
    cap *= 2;
    arr = realloc(arr, sizeof(*arr) + cap * sizeof(arr->u64s[0]));
    if (!arr)
      return zsv_index_status_memory;

    arr->capacity = cap;
    ix->array = arr;
  }

  arr->u64s[len] = line_end;
  arr->len++;

  return zsv_index_status_ok;
}

enum zsv_index_status zsv_index_row_end_offset(const struct zsv_index *ix, uint64_t row, uint64_t *offset_out,
                                               uint64_t *remaining_rows_out) {
  if (row > ix->row_count)
    return zsv_index_status_error;

  if (row < ZSV_INDEX_ROW_N) {
    *offset_out = ix->header_line_end;
    *remaining_rows_out = row;
  } else {
    const size_t i = (row >> ZSV_INDEX_ROW_SHIFT) - 1;

    assert(i < ix->array->len);
    *offset_out = (long)ix->array->u64s[i];
    *remaining_rows_out = row & (ZSV_INDEX_ROW_N - 1);
  }

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
    if (new_line[1] == '\n') {
      *offset += 1;
      return zsv_index_status_ok;
    }

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
