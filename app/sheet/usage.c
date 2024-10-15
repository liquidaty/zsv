
const char *zsvsheet_usage_msg[] = {
  APPNAME ": opens interactive spreadsheet-like viewer in the console", "", "Usage: " APPNAME " filename", "", NULL,
};

static void zsvsheet_usage(void) {
  for (size_t i = 0; zsvsheet_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsvsheet_usage_msg[i]);
}
