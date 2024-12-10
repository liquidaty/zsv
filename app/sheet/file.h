#ifndef ZSVSHEET_FILE_H
#define ZSVSHEET_FILE_H

int zsvsheet_ui_buffer_open_file(const char *filename, const struct zsv_opts *zsv_optsp,
                                 struct zsv_prop_handler *custom_prop_handler,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_bottom,
                                 struct zsvsheet_ui_buffer **ui_buffer_stack_top);

zsvsheet_status zsvsheet_open_file_opts(struct zsvsheet_proc_context *ctx, struct zsvsheet_ui_buffer_opts *opts);
zsvsheet_status zsvsheet_open_file(struct zsvsheet_proc_context *ctx, const char *filepath, struct zsv_opts *zopts);

#endif
