#ifndef ORYN_COMPILER_PARSER_H
#define ORYN_COMPILER_PARSER_H

#include "oryn/compiler/ast.h"
#include "oryn/compiler/token.h"

#include <stddef.h>

typedef struct Parser {
  const char *src, *path;
  size_t pos;
  int line, col;
  Token tok;
} Parser;

int parser_accept(Parser *parser, TokenKind kind);
void parser_expect(Parser *parser, TokenKind kind, const char *description);
char *parser_expect_id(Parser *parser);
Stmts parse_program(Parser *parser);

#endif
