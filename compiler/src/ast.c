#include "oryn/compiler/ast.h"

#include "oryn/compiler/base.h"

#include <stdlib.h>

void push_node(Nodes *items, Node *value) {
  if (items->n == items->cap) {
    items->cap = items->cap ? items->cap * 2 : 4;
    items->v = realloc(items->v, items->cap * sizeof(*items->v));
    if (!items->v) {
      die("out of memory");
    }
  }
  items->v[items->n++] = value;
}

void push_arm(Arms *items, Arm value) {
  if (items->n == items->cap) {
    items->cap = items->cap ? items->cap * 2 : 4;
    items->v = realloc(items->v, items->cap * sizeof(*items->v));
    if (!items->v) {
      die("out of memory");
    }
  }
  items->v[items->n++] = value;
}

void push_stmt(Stmts *items, Stmt value) {
  if (items->n == items->cap) {
    items->cap = items->cap ? items->cap * 2 : 8;
    items->v = realloc(items->v, items->cap * sizeof(*items->v));
    if (!items->v) {
      die("out of memory");
    }
  }
  items->v[items->n++] = value;
}

Node *new_node(NodeKind kind, int line, int col) {
  Node *result = xmalloc(sizeof(*result));
  result->kind = kind;
  result->line = line;
  result->col = col;
  return result;
}
