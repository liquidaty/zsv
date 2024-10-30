#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zsv.h>
#include <zsv/utils/prop.h>

#include "index.h"

static struct zsvsheet_index *add_line_end(struct zsvsheet_index *ix, uint64_t end) {
  size_t len = ix->line_end_len, cap = ix->line_end_capacity;
    
  if (len >= cap) {
    cap *= 2;
    ix = realloc(ix, sizeof(*ix) + cap*sizeof(ix->line_ends[0]));
    if (!ix)
      return NULL;
    
    ix->line_end_capacity = cap;
  }

  ix->line_ends[len] = end;
  ix->line_end_len++;

  return ix;
}

static void build_memory_index_row_handler(void *ctx) {
  struct zsvsheet_indexer *ixr = ctx;
  struct zsvsheet_index *ix = ixr->ix;
  uint64_t line_end = zsv_cum_scanned_length(ixr->parser) + 1;
  
  if(!ixr->ix->header_line_end) {
    ix->header_line_end = line_end;
  } else if((ix->row_count & ((1 << LINE_END_SHIFT) - 1)) == 0) {
    ix = add_line_end(ix, line_end);
    if (!ix) {
      zsv_abort(ixr->parser);
      return;
    }
    
    ixr->ix = ix;
  }

  ix->row_count++;
}

enum zsvsheet_index_status build_memory_index(struct zsvsheet_index_opts *optsp, struct zsvsheet_index **index_out) {
  struct zsvsheet_indexer ixr = {0};
  enum zsvsheet_index_status ret = zsvsheet_index_status_error;
  struct zsv_opts *zopts = optsp->zsv_optsp;
  struct zsv_opts ix_zopts = {0};

  memcpy(&ix_zopts, zopts, sizeof(ix_zopts));
  
  FILE *fp = fopen(optsp->filename, "rb");
  if (!fp)
    goto close_file;

  ix_zopts.ctx = &ixr;
  ix_zopts.stream = fp;
  ix_zopts.row_handler = build_memory_index_row_handler;

  enum zsv_status zst = zsv_new_with_properties(&ix_zopts, optsp->custom_prop_handler, 
                                                optsp->filename, optsp->opts_used, 
                                                &ixr.parser);
  if (zst != zsv_status_ok)
    goto free_parser;
  
  const size_t initial_cap = 256;
  ixr.ix = malloc(sizeof(*ixr.ix) + initial_cap * sizeof(size_t));
  if (!ixr.ix)
    goto free_parser;
  memset(ixr.ix, 0, sizeof(*ixr.ix));
  ixr.ix->line_end_capacity = initial_cap;
 
  while ((zst = zsv_parse_more(ixr.parser)) == zsv_status_ok) 
    ; 

  zsv_finish(ixr.parser);
  
  if (zst == zsv_status_no_more_input) { 
    ret = zsvsheet_index_status_ok;
    *index_out = ixr.ix;
  } else
    free(ixr.ix);

free_parser:
  zsv_delete(ixr.parser);

close_file:
  fclose(fp);

  return ret;
}

void get_memory_index(struct zsvsheet_index *ix, uint64_t row, off_t *offset_out, size_t *remaining_rows_out) {
  const size_t i = row >> LINE_END_SHIFT;
  
  *offset_out = (off_t)ix->line_ends[i];
  *remaining_rows_out = row & ((1 << LINE_END_SHIFT) - 1);
}
