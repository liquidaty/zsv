/**
 * Stub for standalone zsv command compilation e.g. for building zsv_select, zsv_count etc each as separate executables
 */
int main(int argc, const char *argv[]) {
#ifdef ZSV_COMMAND_NO_OPTIONS
  // if ZSV_MAIN_NO_OPTIONS is defined, we just call the command entry point
  // with the original argc and argv arguments
  return ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(argc, argv);
#else
  char opts_used[ZSV_OPTS_SIZE_MAX];
  struct zsv_opts opts;
  enum zsv_status stat = zsv_args_to_opts(argc, argv, &argc, argv, &opts, opts_used);
  if(stat != zsv_status_ok)
    return stat;
  return ZSV_MAIN_FUNC(ZSV_COMMAND)(argc, argv, &opts, NULL, opts_used);
#endif
}
