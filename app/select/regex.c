#ifdef HAVE_PCRE2_8

#include "../utils/pcre2-8/pcre2-8.h"

struct zsv_select_regex {
  struct zsv_select_regex *next;
  const char *pattern;
  regex_handle_t *regex;
  // unsigned char has_anchors:1;
  unsigned char _ : 7;
};

static void zsv_select_regexs_delete(struct zsv_select_regex *rs) {
  for (struct zsv_select_regex *next; rs; rs = next) {
    next = rs->next;
    zsv_pcre2_8_delete(rs->regex);
    free(rs);
  }
}

static void zsv_select_add_regex(struct zsv_select_data *data, const char *pattern) {
  if (pattern && *pattern) {
    struct zsv_select_regex *sr = calloc(1, sizeof(*sr));
    sr->pattern = pattern;
    sr->regex = zsv_pcre2_8_new(pattern, 0);
    if (sr->regex) {
      sr->next = data->search_regexs;
      data->search_regexs = sr;
    } else
      free(sr->regex);
  }
}

#endif
