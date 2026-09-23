#ifndef ORYN_COMPILER_TOKEN_H
#define ORYN_COMPILER_TOKEN_H

typedef enum {
  T_EOF,
  T_ID,
  T_INT,
  T_FLOAT,
  T_STRING,
  T_LET,
  T_FN,
  T_IF,
  T_ELSE,
  T_TRUE,
  T_FALSE,
  T_MATCH,
  T_LP,
  T_RP,
  T_LB,
  T_RB,
  T_LS,
  T_RS,
  T_COMMA,
  T_COLON,
  T_SEMI,
  T_DOT,
  T_PLUS,
  T_MINUS,
  T_STAR,
  T_SLASH,
  T_PERCENT,
  T_EQ,
  T_EQEQ,
  T_NE,
  T_LT,
  T_LE,
  T_GT,
  T_GE,
  T_AND,
  T_OR,
  T_BANG,
  T_ARROW,
  T_FATARROW,
  T_PIPE
} TokenKind;

typedef struct {
  TokenKind kind;
  char *text;
  int line, col;
} Token;

#endif
