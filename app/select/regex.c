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

/**
 * @brief create a full, independent duplicate of the regex list
 */
static struct zsv_select_regex *zsv_select_regexs_dup(struct zsv_select_regex *src) {
  struct zsv_select_regex *head = NULL;
  struct zsv_select_regex **tail = &head;

  while (src) {
    struct zsv_select_regex *new_node = calloc(1, sizeof(*new_node));
    if (!new_node) {
      zsv_select_regexs_delete(head);
      return NULL;
    }

    // Copy the pattern pointer (safe, strings are static/const)
    new_node->pattern = src->pattern;

    // FULL RECOMPILE: Call the standard new() function.
    // This creates a fresh pcre2_code AND a fresh match_data buffer.
    new_node->regex = zsv_pcre2_8_new(src->pattern, 0);

    *tail = new_node;
    tail = &new_node->next;
    src = src->next;
  }
  return head;
}
#endif
