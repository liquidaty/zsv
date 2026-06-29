/*
 * Copyright (C) 2025 Liquidaty and zsv contributors. All rights reserved.
 *
 * This file is part of zsv/lib, distributed under the MIT license as defined at
 * https://opensource.org/licenses/MIT
 */

// implements `select --rename <selector>=<newname>` (see SPEC-zsv-select-rename.md and its
// hash-escape addendum): a streaming, header-only rewrite. <selector> is #N for the 1-based
// input column index (the only way to target a blank-named or one of several duplicate-named
// columns), otherwise a column name (case-insensitive, like -x, renaming every column with
// that name). '#' is the index sigil only when followed by digits; '\' escapes the next char,
// so '\#foo' (or just '#foo', since '#'+non-digit is never an index) names a column literally
// beginning with '#', and '\#5' names the column "#5" (vs. the index #5).

// zsv_select_find_unescaped_eq(): return a pointer to the first '=' that is not preceded by a
// '\' escape, or NULL if none. '\' escapes the following character (including '\' and '=').
static const char *zsv_select_find_unescaped_eq(const char *s) {
  while (*s) {
    if (*s == '\\') {
      if (!s[1]) // trailing lone '\': nothing follows to escape
        return NULL;
      s += 2; // skip the '\' and the character it escapes
    } else if (*s == '=')
      return s;
    else
      s++;
  }
  return NULL;
}

// zsv_select_unescape_dup(): copy [start,end) into a fresh NUL-terminated string, turning each
// "\X" into a literal X. A trailing lone '\' (start[len-1]=='\\') is kept as a literal '\'. The
// result is never longer than the source. Returns NULL on out-of-memory.
static char *zsv_select_unescape_dup(const char *start, const char *end) {
  char *out = malloc((size_t)(end - start) + 1);
  if (!out)
    return NULL;
  char *w = out;
  for (const char *p = start; p < end; p++) {
    if (*p == '\\' && p + 1 < end)
      p++; // drop the backslash; copy the escaped character below
    *w++ = *p;
  }
  *w = '\0';
  return out;
}

// zsv_select_add_rename(): parse one --rename argument and append it to data->renames.
// Returns 0 on success, or nonzero (after printing an error) on a malformed argument.
static int zsv_select_add_rename(struct zsv_select_data *data, const char *arg) {
  // split on the first UNESCAPED '=' so newname may itself contain '=', and a selector name
  // may contain an escaped '\='
  const char *eq = zsv_select_find_unescaped_eq(arg);
  if (!eq || eq == arg)
    return zsv_printerr(1, "--rename: expected <selector>=<newname>, got: %s", arg);
  const char *newname = eq + 1;
  if (!*newname) // empty newname is an error by default
    return zsv_printerr(1, "--rename: empty new name in: %s", arg);

  struct zsv_select_rename *r = calloc(1, sizeof(*r));
  if (!r)
    return zsv_printerr(1, "Out of memory!");
  r->newname = newname; // points into argv, which outlives this command

  // index form only when the raw selector is '#' followed by one or more digits and nothing
  // else (up to '='). '#'+non-digit, or an escaped '\#', falls through to the name branch.
  char is_index = *arg == '#' && eq - arg >= 2;
  for (const char *p = arg + 1; is_index && p < eq; p++)
    if (*p < '0' || *p > '9')
      is_index = 0;

  if (is_index) { // #N, 1-based input column index
    unsigned int n = 0;
    int consumed = 0;
    if (sscanf(arg + 1, "%u%n", &n, &consumed) != 1 || consumed <= 0 || (size_t)consumed + 1 != (size_t)(eq - arg) ||
        n < 1) {
      free(r);
      return zsv_printerr(1, "--rename: invalid column index in: %s", arg);
    }
    r->is_index = 1;
    r->index = n;
  } else { // name selector: un-escape the text before the '=' (\# -> #, \\ -> \, \= -> =)
    if (!(r->selector = zsv_select_unescape_dup(arg, eq))) {
      free(r);
      return zsv_printerr(1, "Out of memory!");
    }
  }

  if (!data->renames_tail)
    data->renames_tail = &data->renames;
  *data->renames_tail = r;
  data->renames_tail = &r->next;
  return 0;
}

// zsv_select_apply_renames(): resolve and apply every --rename against the parsed input header.
// Must run after the header is read (header_names/header_name_count populated) and after
// zsv_select_set_output_columns(), but before the header row is emitted.
// Resolution is two-phase so that (a) name selectors always match the *original* input names
// even after a prior --rename, and (b) a failing directive never produces partial output.
// Returns 0 on success, or nonzero (after printing an error) on an unmatched name, an
// out-of-range index, or two directives targeting the same column.
static int zsv_select_apply_renames(struct zsv_select_data *data) {
  if (!data->renames)
    return 0;

  int err = 0;
  unsigned int n = data->header_name_count;
  // assign[ix] = resolved new name for input column ix (points into argv), or NULL if unchanged.
  // A non-NULL entry also serves as the conflict marker for that column.
  const char **assign = n ? calloc(n, sizeof(*assign)) : NULL;
  if (n && !assign)
    return zsv_printerr(1, "Out of memory!");

  // phase 1: resolve every selector against the original header; detect miss / range / conflict
  for (struct zsv_select_rename *r = data->renames; !err && r; r = r->next) {
    if (r->is_index) {
      if (r->index > n)
        err = zsv_printerr(1, "--rename: column index %u out of range (input has %u column%s)", r->index, n,
                           n == 1 ? "" : "s");
      else if (assign[r->index - 1])
        err = zsv_printerr(1, "--rename: column %u renamed more than once", r->index);
      else
        assign[r->index - 1] = r->newname;
    } else {
      unsigned int matches = 0;
      for (unsigned int ix = 0; !err && ix < n; ix++) {
        unsigned char *h = zsv_select_get_header_name(data, ix);
        if (h && *h && !zsv_stricmp(h, (const unsigned char *)r->selector)) { // blank names ("") are #N-only
          if (assign[ix])
            err = zsv_printerr(1, "--rename: column %u renamed more than once", ix + 1);
          else {
            assign[ix] = r->newname;
            matches++;
          }
        }
      }
      if (!err && !matches)
        err = zsv_printerr(1, "--rename: no column named \"%s\"", r->selector);
    }
  }

  // phase 2: apply (only if no error). Header cells only; data rows are untouched.
  for (unsigned int ix = 0; !err && ix < n; ix++) {
    if (assign[ix]) {
      char *dup = zsv_memdup(assign[ix], strlen(assign[ix]));
      if (!dup) {
        err = zsv_printerr(1, "Out of memory!");
        break;
      }
      free(data->header_names[ix]);
      data->header_names[ix] = (unsigned char *)dup;
    }
  }

  free(assign);
  return err;
}

// zsv_select_renames_delete(): free the --rename list
static void zsv_select_renames_delete(struct zsv_select_rename *r) {
  while (r) {
    struct zsv_select_rename *next = r->next;
    free(r->selector); // NULL for index-form directives
    free(r);
    r = next;
  }
}
