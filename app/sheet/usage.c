
const char *zsv_sheet_usage_msg[] = {
  APPNAME ": opens interactive spreadsheet-like viewer in the console", "", "Usage: " APPNAME " filename", "", NULL,
};

static void zsv_sheet_usage() {
  for (size_t i = 0; zsv_sheet_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_sheet_usage_msg[i]);
}
