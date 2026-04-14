
const char *zsvsheet_usage_msg[] = {
  APPNAME ": opens interactive spreadsheet-like viewer in the console",
  "",
  "Usage: " APPNAME " [options] filename",
  "",
  "Options:",
  "  --compare <spec>   Highlight differences between two column ranges.",
  "                      Ranges separated by 'v' or 'vs'; range delimiters are",
  "                      '-' or ':'. Examples (1-based columns):",
  "                        2-14v15-27  or  2:14 vs 15:27  (both ranges explicit)",
  "                        2-14v15     (right range inferred from left width)",
  "                        2v15-27     (left range inferred from right width)",
  "                        2v15        (width = distance between the two columns)",
  "                      Ranges are auto-trimmed to avoid overlap.",
  "                      Also available interactively as :compare",
  "",
  NULL,
};

static void zsvsheet_usage(void) {
  for (size_t i = 0; zsvsheet_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsvsheet_usage_msg[i]);
}
