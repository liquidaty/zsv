/*
 * Unit test of the sheet command lexer (app/sheet/lexer.c): words, double quotes, escapes,
 * and what makes it reject a command line
 */
#include <stdio.h>
#include <string.h>

#include "../sheet/lexer.c"

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);                                         \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

// Parse `cmd` and hand back the tokens; *status gets the lexer's result
static void lex(const char *cmd, char **toks, int max_toks, int *num_toks, enum zsvsheet_lexer_status *status) {
  static char tokbuf[1024]; // one parse at a time, as the caller uses the tokens before parsing again
  struct zsvsheet_lexer lexer;
  zsvsheet_lexer_init(&lexer, cmd, tokbuf, sizeof(tokbuf), toks, max_toks);
  *status = zsvsheet_lexer_parse(&lexer);
  *num_toks = lexer.num_toks;
}

int main(void) {
  char *toks[8];
  int n;
  enum zsvsheet_lexer_status status;

  lex("hide-column City", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2);
  CHECK(!strcmp(toks[0], "hide-column") && !strcmp(toks[1], "City"));

  // runs of whitespace separate, and surround nothing
  lex("  hide-column \t City  ", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2);
  CHECK(!strcmp(toks[1], "City"));

  lex("", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 0);
  lex("   ", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 0);

  // double quotes group a value with spaces, alone or among bare words
  lex("hide-column \"Accent City\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], "Accent City"));

  lex("a \"b c\" d", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 3);
  CHECK(!strcmp(toks[1], "b c") && !strcmp(toks[2], "d"));

  // single quotes are ordinary characters, so a SQL string literal reaches :sortexpr intact
  lex("sortexpr City = 'New York'", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 5);
  CHECK(!strcmp(toks[3], "'New") && !strcmp(toks[4], "York'"));
  lex("cmd 'abc", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], "'abc"));
  lex("hide-column \"It's\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], "It's"));

  // an escaped double quote, and an empty quoted value (a token of its own)
  lex("hide-column \"a\\\"b\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], "a\"b"));
  lex("cmd \"\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], ""));

  // bytes >= 0x80 (UTF-8) belong to the word they are in
  lex("hide-column Stra\xc3\x9f"
      "e Ort",
      toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 3 &&
        !strcmp(toks[1], "Stra\xc3\x9f"
                         "e"));

  // escapes the lexer knows, and the ones it refuses
  lex("cmd \"a\\tb\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_ok && n == 2 && !strcmp(toks[1], "a\tb"));
  lex("cmd \"a\\qb\"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_error);

  // an unterminated quote, or one with nothing after it
  lex("cmd \"abc", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_error);
  lex("cmd \"", toks, 8, &n, &status);
  CHECK(status == zsvsheet_lexer_status_error);

  // more tokens than the caller's array holds
  lex("a b c d", toks, 3, &n, &status);
  CHECK(status == zsvsheet_lexer_status_nomem);

  return 0;
}
