#include <string.h>

#if 0
#define lexer_debug(FMT, ...) fprintf(stderr, "%s:%d: " FMT "\n", __func__, __LINE__, __VA_ARGS__)
#else
#define lexer_debug(...) ((void)0)
#endif

enum zsvsheet_lexer_status {
  zsvsheet_lexer_status_ok,
  zsvsheet_lexer_status_error,
  zsvsheet_lexer_status_nomem,
  zsvsheet_lexer_status_finish,
};

struct zsvsheet_lexer {
  const char *string;
  const char *rover;
  int left;

  char *tokbuf;
  int tokbufsz;

  char **toks;
  int max_toks;
  int num_toks;
};

void zsvsheet_lexer_init(struct zsvsheet_lexer *lexer, const char *string, char *tokbuf, int tokbufsz, char **toks,
                         int max_toks) {
  lexer_debug("%s", string);
  lexer->string = string;
  lexer->rover = string;
  lexer->left = strlen(string);

  lexer->tokbuf = tokbuf;
  lexer->tokbufsz = tokbufsz;

  lexer->toks = toks;
  lexer->max_toks = max_toks;
  lexer->num_toks = 0;
}

int zsvsheet_lexer_skip_space(struct zsvsheet_lexer *lexer) {
  int orig_left = lexer->left;
  while (lexer->left > 0 && isspace(*lexer->rover)) {
    lexer->rover++;
    lexer->left--;
  }
  return orig_left - lexer->left;
}

int zsvsheet_lexer_skip_not_space(struct zsvsheet_lexer *lexer) {
  int orig_left = lexer->left;
  while (lexer->left > 0 && !isspace(*lexer->rover)) {
    lexer->rover++;
    lexer->left--;
  }
  return orig_left - lexer->left;
}

enum zsvsheet_lexer_status zsvsheet_lexer_append_char(struct zsvsheet_lexer *lexer, char c) {
  if (!lexer->tokbufsz)
    return zsvsheet_lexer_status_nomem;
  *(lexer->tokbuf++) = c;
  lexer->tokbufsz--;
  return zsvsheet_lexer_status_ok;
}

enum zsvsheet_lexer_status zsvsheet_lexer_append_escaped_char(struct zsvsheet_lexer *lexer, char c) {
  switch (c) {
  case 'a':
    return zsvsheet_lexer_append_char(lexer, (char)7);
  case 'b':
    return zsvsheet_lexer_append_char(lexer, (char)8);
  case 't':
    return zsvsheet_lexer_append_char(lexer, (char)9);
  case 'n':
    return zsvsheet_lexer_append_char(lexer, (char)10);
  case 'v':
    return zsvsheet_lexer_append_char(lexer, (char)11);
  case 'f':
    return zsvsheet_lexer_append_char(lexer, (char)12);
  case 'r':
    return zsvsheet_lexer_append_char(lexer, (char)13);
  case '"':
    return zsvsheet_lexer_append_char(lexer, (char)'"');
  case '\\':
    return zsvsheet_lexer_append_char(lexer, (char)'\\');
  // We don't allow for escaped zeros, our tokens are null terminated.
  // case '0': *(s++) = 0; break;
  default:
    lexer_debug("Invalid escape character '\\%c'", c);
  }
  return zsvsheet_lexer_status_error;
}

enum zsvsheet_lexer_status zsvsheet_lexer_extract_string(struct zsvsheet_lexer *lexer) {
  enum {
    ESCAPE = 0x1,
    START_QUOTE = 0x2,
    END_QUOTE = 0x4,
  };
  unsigned flags = 0;
  char *tokbuf_start = lexer->tokbuf;
  enum zsvsheet_lexer_status status;

  if (lexer->num_toks >= lexer->max_toks)
    return zsvsheet_lexer_status_nomem;
  if (lexer->left < 2 || *lexer->rover != '"')
    return zsvsheet_lexer_status_error;

  flags |= START_QUOTE;
  lexer->rover++;
  lexer->left--;

  while (lexer->left) {
    if (flags & ESCAPE) {
      if ((status = zsvsheet_lexer_append_escaped_char(lexer, *lexer->rover)))
        return status;
      flags &= ~ESCAPE;
    } else {
      if (*lexer->rover == '"') {
        flags |= END_QUOTE;
        lexer->rover++;
        lexer->left--;
        break;
      } else if (*lexer->rover == '\\') {
        flags |= ESCAPE;
      } else {
        if ((status = zsvsheet_lexer_append_char(lexer, *lexer->rover)))
          return status;
      }
    }
    lexer->rover++;
    lexer->left--;
  }

  if (flags & START_QUOTE) {
    if (!(flags & END_QUOTE)) {
      lexer_debug("Unterminated string: %s", lexer->string);
      return zsvsheet_lexer_status_error;
    }
  } else
    return 0;

  if ((status = zsvsheet_lexer_append_char(lexer, '\0')))
    return status;

  lexer->toks[lexer->num_toks] = tokbuf_start;
  lexer->num_toks += 1;

  return zsvsheet_lexer_status_ok;
}

enum zsvsheet_lexer_status zsvsheet_lexer_extract_word(struct zsvsheet_lexer *lexer) {
  int extracted;
  const char *token = lexer->rover;
  extracted = zsvsheet_lexer_skip_not_space(lexer);
  if (extracted == 0)
    return zsvsheet_lexer_status_error;
  if (extracted + 1 > lexer->tokbufsz)
    return zsvsheet_lexer_status_nomem;
  if (lexer->num_toks >= lexer->max_toks)
    return zsvsheet_lexer_status_nomem;
  memcpy(lexer->tokbuf, token, extracted);
  lexer->tokbuf[extracted] = '\0';
  lexer->toks[lexer->num_toks] = lexer->tokbuf;
  lexer->num_toks += 1;
  lexer->tokbuf += extracted + 1;
  lexer->tokbufsz -= extracted + 1;
  return zsvsheet_lexer_status_ok;
}

enum zsvsheet_lexer_status zsvsheet_lexer_parse_token(struct zsvsheet_lexer *lexer) {
  enum zsvsheet_lexer_status status;

  zsvsheet_lexer_skip_space(lexer);
  if (!lexer->left)
    return zsvsheet_lexer_status_finish;

  if (*lexer->rover == '"')
    status = zsvsheet_lexer_extract_string(lexer);
  else
    status = zsvsheet_lexer_extract_word(lexer);
  lexer_debug("status=%d num_toks=%d left=%d", status, lexer->num_toks, lexer->left);
  return status;
}

enum zsvsheet_lexer_status zsvsheet_lexer_parse(struct zsvsheet_lexer *lexer) {
  enum zsvsheet_lexer_status status;
  while (true) {
    status = zsvsheet_lexer_parse_token(lexer);
    lexer_debug("status=%d", status);
    if (status == zsvsheet_lexer_status_ok)
      continue;
    else
      break;
  }
  if (status == zsvsheet_lexer_status_finish)
    status = zsvsheet_lexer_status_ok;
  return status;
}
