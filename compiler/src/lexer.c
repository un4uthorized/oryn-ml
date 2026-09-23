#include "oryn/compiler/lexer.h"

#include "oryn/compiler/base.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int peek(Parser *p) {
  return p->src[p->pos];
}

static int take(Parser *p) {
  int c = peek(p);
  if (c) {
    p->pos++;
    if (c == '\n') {
      p->line++;
      p->col = 1;
    } else {
      p->col++;
    }
  }
  return c;
}

static TokenKind keyword(const char *s) {
  if (!strcmp(s, "let")) {
    return T_LET;
  }
  if (!strcmp(s, "fn")) {
    return T_FN;
  }
  if (!strcmp(s, "if")) {
    return T_IF;
  }
  if (!strcmp(s, "else")) {
    return T_ELSE;
  }
  if (!strcmp(s, "true")) {
    return T_TRUE;
  }
  if (!strcmp(s, "false")) {
    return T_FALSE;
  }
  if (!strcmp(s, "match")) {
    return T_MATCH;
  }
  return T_ID;
}

void lexer_next(Parser *p) {
  free(p->tok.text);
  p->tok.text = NULL;
  for (;;) {
    while (isspace(peek(p))) {
      take(p);
    }
    if (peek(p) == '/' && p->src[p->pos + 1] == '/') {
      while (peek(p) && peek(p) != '\n') {
        take(p);
      }
      continue;
    }
    break;
  }
  int line = p->line, col = p->col, c = take(p);
  p->tok = (Token){T_EOF, NULL, line, col};
  if (!c) {
    return;
  }
  if (isalpha(c) || c == '_') {
    size_t start = p->pos - 1;
    while (isalnum(peek(p)) || peek(p) == '_') {
      take(p);
    }
    p->tok.text = xstrndup(p->src + start, p->pos - start);
    p->tok.kind = keyword(p->tok.text);
    return;
  }
  if (isdigit(c)) {
    size_t start = p->pos - 1;
    int is_float = 0;
    while (isdigit(peek(p))) {
      take(p);
    }
    if (peek(p) == '.' && isdigit(p->src[p->pos + 1])) {
      is_float = 1;
      take(p);
      while (isdigit(peek(p))) {
        take(p);
      }
    }
    p->tok.text = xstrndup(p->src + start, p->pos - start);
    p->tok.kind = is_float ? T_FLOAT : T_INT;
    return;
  }
  if (c == '"') {
    size_t cap = 32, n = 0;
    char *buffer = xmalloc(cap);
    while (peek(p) && peek(p) != '"') {
      int q = take(p);
      if (q == '\\') {
        q = take(p);
        if (q == 'n') {
          q = '\n';
        } else if (q == 't') {
          q = '\t';
        } else if (q == 'r') {
          q = '\r';
        }
      }
      if (n + 1 >= cap) {
        cap *= 2;
        buffer = realloc(buffer, cap);
      }
      buffer[n++] = (char)q;
    }
    if (take(p) != '"') {
      die("%s:%d:%d: unterminated string", p->path, line, col);
    }
    buffer[n] = 0;
    p->tok.kind = T_STRING;
    p->tok.text = buffer;
    return;
  }
#define TWO(ch, one, two)                                                                          \
  do {                                                                                             \
    if (peek(p) == (ch)) {                                                                         \
      take(p);                                                                                     \
      p->tok.kind = (two);                                                                         \
    } else                                                                                         \
      p->tok.kind = (one);                                                                         \
  } while (0)
  switch (c) {
  case '(':
    p->tok.kind = T_LP;
    break;
  case ')':
    p->tok.kind = T_RP;
    break;
  case '{':
    p->tok.kind = T_LB;
    break;
  case '}':
    p->tok.kind = T_RB;
    break;
  case '[':
    p->tok.kind = T_LS;
    break;
  case ']':
    p->tok.kind = T_RS;
    break;
  case ',':
    p->tok.kind = T_COMMA;
    break;
  case ':':
    p->tok.kind = T_COLON;
    break;
  case ';':
    p->tok.kind = T_SEMI;
    break;
  case '.':
    p->tok.kind = T_DOT;
    break;
  case '+':
    p->tok.kind = T_PLUS;
    break;
  case '-':
    TWO('>', T_MINUS, T_ARROW);
    break;
  case '*':
    p->tok.kind = T_STAR;
    break;
  case '/':
    p->tok.kind = T_SLASH;
    break;
  case '%':
    p->tok.kind = T_PERCENT;
    break;
  case '=':
    TWO('=', T_EQ, T_EQEQ);
    if (p->tok.kind == T_EQ && peek(p) == '>') {
      take(p);
      p->tok.kind = T_FATARROW;
    }
    break;
  case '!':
    TWO('=', T_BANG, T_NE);
    break;
  case '<':
    TWO('=', T_LT, T_LE);
    break;
  case '>':
    TWO('=', T_GT, T_GE);
    break;
  case '&':
    if (take(p) != '&') {
      die("%s:%d:%d: expected &&", p->path, line, col);
    }
    p->tok.kind = T_AND;
    break;
  case '|':
    if (peek(p) == '>') {
      take(p);
      p->tok.kind = T_PIPE;
    } else if (peek(p) == '|') {
      take(p);
      p->tok.kind = T_OR;
    } else {
      die("%s:%d:%d: unexpected |", p->path, line, col);
    }
    break;
  default:
    die("%s:%d:%d: unexpected character '%c'", p->path, line, col, c);
  }
#undef TWO
}

int parser_accept(Parser *p, TokenKind kind) {
  if (p->tok.kind != kind) {
    return 0;
  }
  lexer_next(p);
  return 1;
}

void parser_expect(Parser *p, TokenKind kind, const char *description) {
  if (!parser_accept(p, kind)) {
    die("%s:%d:%d: expected %s", p->path, p->tok.line, p->tok.col, description);
  }
}

char *parser_expect_id(Parser *p) {
  if (p->tok.kind != T_ID) {
    die("%s:%d:%d: expected identifier", p->path, p->tok.line, p->tok.col);
  }
  char *name = xstrdup(p->tok.text);
  lexer_next(p);
  return name;
}
