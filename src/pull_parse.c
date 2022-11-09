struct _zsv_pull_row {
  size_t cell_count;
  unsigned char **values;
  unsigned *lengths;
};

struct zsv_chunk_row_internal {
  struct _zsv_pull_row r;
  // internal
  struct zsv_chunk_row_internal *next;
  unsigned char need_to_free_values:1;
  unsigned char _:7;
};

struct zsv_pull_data {
  struct zsv_chunk_row_internal *rows, **next_row, *current_row;
  enum zsv_status stat, parse_more_status;
  zsv_parser parser;
};

static void zsv_chunk_row_free(struct zsv_chunk_row_internal *r) {
  if(r) {
    if(r->r.values && r->need_to_free_values) {
      for(size_t i = 0; i < r->r.cell_count; i++)
        free(r->r.values[i]);
    }
    free(r->r.values);
    free(r->r.lengths);
    free(r);
  }
}

static void zsv_pull_free_rows(struct zsv_pull_data *data) {
  for(struct zsv_chunk_row_internal *r = data->rows, *next; r; r = next) {
    next = r->next;
    zsv_chunk_row_free(r);
  }
  data->rows = data->current_row = NULL;
}

static void zsv_pull_data_free(struct zsv_pull_data *data) {
  if(data) {
    zsv_pull_free_rows(data);
    free(data);
  }
}

void zsv_pull_fetch_more(zsv_parser parser, struct zsv_pull_data *data) {
  // we could make this MUCH memory efficient by recycling row pointers...
  // but we will leave that for another day
  zsv_pull_free_rows(data);
  while(data->rows == NULL && data->parse_more_status == zsv_status_ok)
    data->parse_more_status = zsv_parse_more(parser);
  if(!data->rows && data->parse_more_status == zsv_status_no_more_input
     && !parser->finished)
    zsv_finish(parser);
  data->current_row = data->rows;
}

/**
 * save this row so that it can be fetched by zsv_pull_next_row()
 */
void zsv_pull_row_handler(void *ctx) {
  // we could make this more memory efficient by recycling row pointers...
  // but we will leave that for another day
  fprintf(stderr, "  zsv_pull_row_handler\n");

  struct zsv_pull_data *data = ctx;
  if(data->stat != zsv_status_ok)
    return;
  unsigned int columns_used = zsv_cell_count(data->parser);
  struct zsv_chunk_row_internal *r = calloc(1, sizeof(*r));
  if(!r) {
    data->stat = zsv_status_memory;
    return;
  }

  if(!data->rows)
    data->rows = r;
  else
    *data->next_row = r;
  data->next_row = &r->next;

  if(!columns_used)
    return;

  r->r.values = calloc(columns_used, sizeof(*r->r.values));
  r->r.lengths = calloc(columns_used, sizeof(*r->r.lengths));

  if(!(r->r.values && r->r.lengths))
    data->stat = zsv_status_memory;
  else {
    r->r.cell_count = columns_used;

    // the first (header) row is not guaranteed to be memory-stable
    if(data->current_row == 0) {
      r->need_to_free_values = 1;
      for(unsigned i = 0; i < columns_used; i++) {
        struct zsv_cell cell = zsv_get_cell(data->parser, i);
        if(cell.len) {
          if(!(r->r.values[i] = malloc(cell.len))) {
            data->stat = zsv_status_memory;
            break;
          } else {
            memcpy(r->r.values[i], cell.str, cell.len);
            r->r.lengths[i] = cell.len;
          }
        }
      }
    } else {
      // subsequent rows are memory-stable so we don't need to copy content, only positions
      for(unsigned i = 0; i < columns_used; i++) {
        struct zsv_cell cell = zsv_get_cell(data->parser, i);
        if(cell.len) {
          r->r.values[i] = cell.str;
          r->r.lengths[i] = cell.len;
        }
      }
    }
  }
}
