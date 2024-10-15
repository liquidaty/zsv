#ifndef ZSVSHEET_FILE_H
#define ZSVSHEET_FILE_H

int zsvsheet_ui_buffer_open_file(const char *filename, const struct zsv_opts *zsv_optsp, const char *row_filter,
                                 struct zsv_prop_handler *custom_prop_handler, const char *opts_used,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_top);

#endif
