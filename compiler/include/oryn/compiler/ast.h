#ifndef ORYN_COMPILER_AST_H
#define ORYN_COMPILER_AST_H

#include <stddef.h>

typedef enum {
  N_INT,
  N_FLOAT,
  N_STRING,
  N_BOOL,
  N_VAR,
  N_ARRAY,
  N_UNARY,
  N_BINARY,
  N_CALL,
  N_METHOD,
  N_LAMBDA,
  N_IF,
  N_MATCH
} NodeKind;

typedef struct Node Node;

typedef struct {
  Node **v;
  size_t n, cap;
} Nodes;

typedef struct {
  char *ctor, *bind;
  Node *body;
} Arm;

typedef struct {
  Arm *v;
  size_t n, cap;
} Arms;

struct Node {
  NodeKind kind;
  char *s;
  long i;
  double f;
  int op;
  Node *a, *b, *c;
  Nodes xs;
  Arms arms;
  char **params, **param_types, **captures;
  size_t nparams, ncaptures;
  int lambda_id;
  int line, col;
};

typedef enum {
  S_LET,
  S_FN,
  S_EXPR
} StmtKind;

typedef struct {
  StmtKind kind;
  char *name;
  char **params;
  char **param_types;
  size_t nparams;
  char *type;
  Node *expr;
  int line, col;
} Stmt;

typedef struct {
  Stmt *v;
  size_t n, cap;
} Stmts;

void push_node(Nodes *items, Node *node_value);
void push_arm(Arms *items, Arm arm);
void push_stmt(Stmts *items, Stmt stmt);
Node *new_node(NodeKind kind, int line, int col);

#endif
