
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
  "Editing:",
  "  i (or :cell <value>) edits the cell under the cursor; :w [file] saves the",
  "  buffer as CSV (LF line ends, no BOM, quoting only where needed), and :q!",
  "  quits without saving. Saving over the opened file is refused when the file",
  "  would read back differently as CSV (e.g. tab-delimited, or opened with -R).",
  "  :editmode types keys into cells as in a spreadsheet (Enter, Tab and arrows",
  "  store and move, F2 edits the current value, <ctrl>c/<ctrl>v copy and paste,",
  "  <ctrl>s saves, <esc> leaves). With unsaved changes, :q asks you to type yes.",
  "  <ctrl>c (or :copy) copies the cell under the cursor to the system clipboard,",
  "  in edit mode along with sheet's own clipboard for <ctrl>v; a message on the",
  "  status line clears after a few seconds (ZSVSHEET_STATUS_TIMEOUT_MS sets that,",
  "  0 to keep it until replaced). :quithint shows how to quit.",
  "",
  NULL,
};

static void zsvsheet_usage(void) {
  for (size_t i = 0; zsvsheet_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsvsheet_usage_msg[i]);
}
