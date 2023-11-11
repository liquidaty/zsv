#ifndef ZSV_COMPARE_H
#define ZSV_COMPARE_H

// public
enum zsv_compare_status {
  zsv_compare_status_ok = 0,
  zsv_compare_status_memory,
  zsv_compare_status_no_data,
  zsv_compare_status_no_more_input = 100,
  zsv_compare_status_error = 199
};

typedef struct zsv_compare_data *zsv_compare_handle;

typedef int (*zsv_compare_cell_func)(void *ctx, struct zsv_cell, struct zsv_cell,
                                     void *struct_zsv_compare_data,
                                     unsigned input_col_ix);

zsv_compare_handle zsv_compare_new();
// enum zsv_compare_status zsv_compare_set_inputs(zsv_compare_handle, unsigned input_count, unsigned key_count);
void zsv_compare_set_input_parser(zsv_compare_handle cmp, zsv_parser p, unsigned ix);
void zsv_compare_delete(zsv_compare_handle);
void zsv_compare_set_comparison(zsv_compare_handle, zsv_compare_cell_func, void *);

#endif
