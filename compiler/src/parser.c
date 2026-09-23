#include "oryn/compiler/parser.h"

#include "oryn/compiler/base.h"
#include "oryn/compiler/lexer.h"

#include <stdlib.h>
#include <string.h>

static Node *parse_expression(Parser *p, int min_precedence);

static char *append_text(char *left, const char *right) {
  size_t a = strlen(left), b = strlen(right);
  char *joined = realloc(left, a + b + 1);
  if (!joined) {
    die("out of memory");
  }
  memcpy(joined + a, right, b + 1);
  return joined;
}

static char *parse_type_name(Parser *p) {
  if (p->tok.kind != T_ID) {
    die("%s:%d:%d: expected type name", p->path, p->tok.line, p->tok.col);
  }
  char *type = xstrdup(p->tok.text);
  lexer_next(p);
  if (parser_accept(p, T_LT)) {
    type = append_text(type, "<");
    for (;;) {
      char *argument = parse_type_name(p);
      type = append_text(type, argument);
      free(argument);
      if (!parser_accept(p, T_COMMA)) {
        break;
      }
      type = append_text(type, ",");
    }
    parser_expect(p, T_GT, "'>' after type arguments");
    type = append_text(type, ">");
  }
  return type;
}

static char *parse_type(Parser *p) {
  return parser_accept(p, T_COLON) ? parse_type_name(p) : NULL;
}

static Node *parse_primary(Parser *p) {
  int line = p->tok.line, col = p->tok.col;
  Node *n;
  if (parser_accept(p, T_FN)) {
    n = new_node(N_LAMBDA, line, col);
    size_t cap = 0;
    int parenthesized = parser_accept(p, T_LP);
    do {
      if (n->nparams == cap) {
        cap = cap ? cap * 2 : 2;
        n->params = realloc(n->params, cap * sizeof(*n->params));
        n->param_types = realloc(n->param_types, cap * sizeof(*n->param_types));
        if (!n->params || !n->param_types) {
          die("out of memory");
        }
      }
      n->params[n->nparams] = parser_expect_id(p);
      n->param_types[n->nparams++] = parse_type(p);
    } while (parenthesized && parser_accept(p, T_COMMA));
    if (parenthesized) {
      parser_expect(p, T_RP, "')'");
    }
    parser_expect(p, T_FATARROW, "'=>' after anonymous function parameters");
    n->a = parse_expression(p, 0);
    return n;
  }
  if (p->tok.kind == T_INT) {
    n = new_node(N_INT, line, col);
    n->i = strtol(p->tok.text, NULL, 10);
    lexer_next(p);
    return n;
  }
  if (p->tok.kind == T_FLOAT) {
    n = new_node(N_FLOAT, line, col);
    n->f = strtod(p->tok.text, NULL);
    lexer_next(p);
    return n;
  }
  if (p->tok.kind == T_STRING) {
    n = new_node(N_STRING, line, col);
    n->s = xstrdup(p->tok.text);
    lexer_next(p);
    return n;
  }
  if (parser_accept(p, T_TRUE)) {
    n = new_node(N_BOOL, line, col);
    n->i = 1;
    return n;
  }
  if (parser_accept(p, T_FALSE)) {
    return new_node(N_BOOL, line, col);
  }
  if (p->tok.kind == T_ID) {
    n = new_node(N_VAR, line, col);
    n->s = parser_expect_id(p);
    return n;
  }
  if (parser_accept(p, T_LP)) {
    n = parse_expression(p, 0);
    parser_expect(p, T_RP, "')'");
    return n;
  }
  if (parser_accept(p, T_LS)) {
    n = new_node(N_ARRAY, line, col);
    if (!parser_accept(p, T_RS)) {
      do {
        push_node(&n->xs, parse_expression(p, 0));
      } while (parser_accept(p, T_COMMA));
      parser_expect(p, T_RS, "']'");
    }
    return n;
  }
  if (parser_accept(p, T_IF)) {
    n = new_node(N_IF, line, col);
    n->a = parse_expression(p, 0);
    parser_expect(p, T_LB, "'{' after if condition");
    n->b = parse_expression(p, 0);
    parser_accept(p, T_SEMI);
    parser_expect(p, T_RB, "'}'");
    parser_expect(p, T_ELSE, "else");
    parser_expect(p, T_LB, "'{'");
    n->c = parse_expression(p, 0);
    parser_accept(p, T_SEMI);
    parser_expect(p, T_RB, "'}'");
    return n;
  }
  if (parser_accept(p, T_MATCH)) {
    n = new_node(N_MATCH, line, col);
    n->a = parse_expression(p, 0);
    parser_expect(p, T_LB, "'{' after match value");
    while (p->tok.kind != T_RB) {
      Arm arm = {0};
      arm.ctor = parser_expect_id(p);
      if (parser_accept(p, T_LP)) {
        arm.bind = parser_expect_id(p);
        parser_expect(p, T_RP, "')'");
      }
      parser_expect(p, T_FATARROW, "'=>'");
      arm.body = parse_expression(p, 0);
      push_arm(&n->arms, arm);
      if (!parser_accept(p, T_COMMA)) {
        parser_accept(p, T_SEMI);
      }
    }
    parser_expect(p, T_RB, "'}'");
    return n;
  }
  die("%s:%d:%d: expected expression", p->path, p->tok.line, p->tok.col);
  return NULL;
}

static Node *parse_postfix(Parser *p) {
  Node *n = parse_primary(p);
  for (;;) {
    if (parser_accept(p, T_LP)) {
      Node *call = new_node(N_CALL, n->line, n->col);
      call->a = n;
      if (!parser_accept(p, T_RP)) {
        do {
          push_node(&call->xs, parse_expression(p, 0));
        } while (parser_accept(p, T_COMMA));
        parser_expect(p, T_RP, "')'");
      }
      n = call;
      continue;
    }
    if (parser_accept(p, T_DOT)) {
      Node *method = new_node(N_METHOD, n->line, n->col);
      method->a = n;
      method->s = parser_expect_id(p);
      parser_expect(p, T_LP, "'('");
      if (!parser_accept(p, T_RP)) {
        do {
          push_node(&method->xs, parse_expression(p, 0));
        } while (parser_accept(p, T_COMMA));
        parser_expect(p, T_RP, "')'");
      }
      n = method;
      continue;
    }
    return n;
  }
}

static int precedence(TokenKind kind) {
  switch (kind) {
  case T_PIPE:
    return 1;
  case T_OR:
    return 2;
  case T_AND:
    return 3;
  case T_EQEQ:
  case T_NE:
    return 4;
  case T_LT:
  case T_LE:
  case T_GT:
  case T_GE:
    return 5;
  case T_PLUS:
  case T_MINUS:
    return 6;
  case T_STAR:
  case T_SLASH:
  case T_PERCENT:
    return 7;
  default:
    return 0;
  }
}

static Node *parse_expression(Parser *p, int min_precedence) {
  Node *left;
  if (p->tok.kind == T_MINUS || p->tok.kind == T_BANG) {
    int op = p->tok.kind, line = p->tok.line, col = p->tok.col;
    lexer_next(p);
    left = new_node(N_UNARY, line, col);
    left->op = op;
    left->a = parse_expression(p, 8);
  } else {
    left = parse_postfix(p);
  }
  while (precedence(p->tok.kind) > min_precedence) {
    int op = p->tok.kind, prec = precedence(p->tok.kind), line = p->tok.line, col = p->tok.col;
    lexer_next(p);
    Node *right = parse_expression(p, prec);
    if (op == T_PIPE) {
      if (right->kind == N_VAR) {
        Node *call = new_node(N_CALL, line, col);
        call->a = right;
        push_node(&call->xs, left);
        left = call;
      } else if (right->kind == N_CALL) {
        Nodes args = {0};
        push_node(&args, left);
        for (size_t i = 0; i < right->xs.n; i++) {
          push_node(&args, right->xs.v[i]);
        }
        right->xs = args;
        left = right;
      } else {
        die("%s:%d: pipeline target must be a function", p->path, line);
      }
    } else {
      Node *binary = new_node(N_BINARY, line, col);
      binary->op = op;
      binary->a = left;
      binary->b = right;
      left = binary;
    }
  }
  return left;
}

Stmts parse_program(Parser *p) {
  Stmts program = {0};
  lexer_next(p);
  while (p->tok.kind != T_EOF) {
    Stmt stmt = {0};
    stmt.line = p->tok.line;
    stmt.col = p->tok.col;
    if (parser_accept(p, T_LET)) {
      stmt.kind = S_LET;
      stmt.name = parser_expect_id(p);
      stmt.type = parse_type(p);
      parser_expect(p, T_EQ, "'='");
      stmt.expr = parse_expression(p, 0);
    } else if (parser_accept(p, T_FN)) {
      stmt.kind = S_FN;
      stmt.name = parser_expect_id(p);
      parser_expect(p, T_LP, "'('");
      size_t cap = 0;
      if (!parser_accept(p, T_RP)) {
        do {
          if (stmt.nparams == cap) {
            cap = cap ? cap * 2 : 4;
            stmt.params = realloc(stmt.params, cap * sizeof(*stmt.params));
            stmt.param_types = realloc(stmt.param_types, cap * sizeof(*stmt.param_types));
            if (!stmt.params || !stmt.param_types) {
              die("out of memory");
            }
          }
          stmt.params[stmt.nparams++] = parser_expect_id(p);
          stmt.param_types[stmt.nparams - 1] = parse_type(p);
        } while (parser_accept(p, T_COMMA));
        parser_expect(p, T_RP, "')'");
      }
      if (parser_accept(p, T_ARROW)) {
        stmt.type = parse_type_name(p);
      }
      if (parser_accept(p, T_EQ)) {
        stmt.expr = parse_expression(p, 0);
      } else {
        parser_expect(p, T_LB, "'{' or '='");
        stmt.expr = parse_expression(p, 0);
        parser_accept(p, T_SEMI);
        parser_expect(p, T_RB, "'}'");
      }
    } else {
      stmt.kind = S_EXPR;
      stmt.expr = parse_expression(p, 0);
    }
    parser_accept(p, T_SEMI);
    push_stmt(&program, stmt);
  }
  return program;
}
