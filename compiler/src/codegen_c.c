#include "oryn/compiler/base.h"
#include "oryn/compiler/codegen.h"
#include "oryn/compiler/token.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static Node *current_lambda;
static Node **lambdas;
static size_t lambda_count, lambda_cap;

static void emit_string(FILE *out, const char *s) {
  fputc('"', out);
  for (; *s; s++) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') {
      fprintf(out, "\\%c", c);
    } else if (c == '\n') {
      fputs("\\n", out);
    } else if (c == '\t') {
      fputs("\\t", out);
    } else if (c < 32) {
      fprintf(out, "\\x%02x", c);
    } else {
      fputc(c, out);
    }
  }
  fputc('"', out);
}

static void emit_identifier(FILE *out, const char *s) {
  fputs("oryn_", out);
  for (; *s; s++) {
    fputc(isalnum((unsigned char)*s) || *s == '_' ? *s : '_', out);
  }
}

static int name_in(char **names, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) {
    if (!strcmp(names[i], name)) {
      return 1;
    }
  }
  return 0;
}

static void capture(Node *lambda, const char *name) {
  if (name_in(lambda->params, lambda->nparams, name) ||
      name_in(lambda->captures, lambda->ncaptures, name)) {
    return;
  }
  lambda->captures = realloc(lambda->captures, (lambda->ncaptures + 1) * sizeof(*lambda->captures));
  if (!lambda->captures) {
    die("out of memory");
  }
  lambda->captures[lambda->ncaptures++] = (char *)name;
}

static void find_captures(Node *n, Node *lambda, char **visible, size_t count) {
  if (!n || (n->kind == N_LAMBDA && n != lambda)) {
    return;
  }
  if (n->kind == N_VAR && name_in(visible, count, n->s)) {
    capture(lambda, n->s);
  }
  find_captures(n->a, lambda, visible, count);
  find_captures(n->b, lambda, visible, count);
  find_captures(n->c, lambda, visible, count);
  for (size_t i = 0; i < n->xs.n; i++) {
    find_captures(n->xs.v[i], lambda, visible, count);
  }
  for (size_t i = 0; i < n->arms.n; i++) {
    find_captures(n->arms.v[i].body, lambda, visible, count);
  }
}

static void discover(Node *n, char **visible, size_t count) {
  if (!n) {
    return;
  }
  if (n->kind == N_LAMBDA) {
    n->lambda_id = (int)lambda_count;
    if (lambda_count == lambda_cap) {
      lambda_cap = lambda_cap ? lambda_cap * 2 : 8;
      lambdas = realloc(lambdas, lambda_cap * sizeof(*lambdas));
      if (!lambdas) {
        die("out of memory");
      }
    }
    lambdas[lambda_count++] = n;
    find_captures(n->a, n, visible, count);
  }
  discover(n->a, visible, count);
  discover(n->b, visible, count);
  discover(n->c, visible, count);
  for (size_t i = 0; i < n->xs.n; i++) {
    discover(n->xs.v[i], visible, count);
  }
  for (size_t i = 0; i < n->arms.n; i++) {
    discover(n->arms.v[i].body, visible, count);
  }
}

static void emit_expression(FILE *out, Node *n);

static void emit_args(FILE *out, Nodes *args) {
  fprintf(out, "%zu, (OValue[]){", args->n);
  for (size_t i = 0; i < args->n; i++) {
    if (i) {
      fputc(',', out);
    }
    emit_expression(out, args->v[i]);
  }
  fputc('}', out);
}

static const char *operator_name(int op) {
  switch (op) {
  case T_PLUS:
    return "add";
  case T_MINUS:
    return "sub";
  case T_STAR:
    return "mul";
  case T_SLASH:
    return "div";
  case T_PERCENT:
    return "mod";
  case T_EQEQ:
    return "eq";
  case T_NE:
    return "ne";
  case T_LT:
    return "lt";
  case T_LE:
    return "le";
  case T_GT:
    return "gt";
  case T_GE:
    return "ge";
  case T_AND:
    return "and";
  case T_OR:
    return "or";
  default:
    return "bad";
  }
}

static void emit_expression(FILE *out, Node *n) {
  switch (n->kind) {
  case N_INT:
    fprintf(out, "ov_int(%ld)", n->i);
    break;
  case N_FLOAT:
    fprintf(out, "ov_float(%.17g)", n->f);
    break;
  case N_STRING:
    fputs("ov_string(", out);
    emit_string(out, n->s);
    fputc(')', out);
    break;
  case N_BOOL:
    fprintf(out, "ov_bool(%ld)", n->i);
    break;
  case N_VAR:
    if (current_lambda) {
      for (size_t i = 0; i < current_lambda->ncaptures; i++) {
        if (!strcmp(current_lambda->captures[i], n->s)) {
          fprintf(out, "_env[%zu]", i);
          return;
        }
      }
    }
    emit_identifier(out, n->s);
    break;
  case N_ARRAY:
    fputs("ov_array(", out);
    emit_args(out, &n->xs);
    fputc(')', out);
    break;
  case N_LAMBDA:
    fprintf(out, "ov_function(oryn_lambda_%d,%zu,(OValue[]){", n->lambda_id, n->ncaptures);
    for (size_t i = 0; i < n->ncaptures; i++) {
      if (i) {
        fputc(',', out);
      }
      emit_identifier(out, n->captures[i]);
    }
    fputs("})", out);
    break;
  case N_UNARY:
    fputs(n->op == T_MINUS ? "ov_neg(" : "ov_not(", out);
    emit_expression(out, n->a);
    fputc(')', out);
    break;
  case N_BINARY:
    fprintf(out, "ov_%s(", operator_name(n->op));
    emit_expression(out, n->a);
    fputc(',', out);
    emit_expression(out, n->b);
    fputc(')', out);
    break;
  case N_CALL:
    if (n->a->kind != N_VAR) {
      die("line %d: only named functions can be called in v0.1", n->line);
    }
    if (!strcmp(n->a->s, "Some") || !strcmp(n->a->s, "Ok") || !strcmp(n->a->s, "Err")) {
      if (n->xs.n != 1) {
        die("line %d: constructor expects one argument", n->line);
      }
      fprintf(out,
              "ov_%s(",
              !strcmp(n->a->s, "Some") ? "some"
              : !strcmp(n->a->s, "Ok") ? "ok"
                                       : "err");
      emit_expression(out, n->xs.v[0]);
      fputc(')', out);
    } else {
      emit_identifier(out, n->a->s);
      fputc('(', out);
      emit_args(out, &n->xs);
      fputc(')', out);
    }
    break;
  case N_METHOD:
    if (n->a->kind == N_VAR && (!strcmp(n->a->s, "Int") || !strcmp(n->a->s, "Float")) &&
        !strcmp(n->s, "parse")) {
      fputs(!strcmp(n->a->s, "Int") ? "oryn_parse_int(" : "oryn_parse_float(", out);
      emit_args(out, &n->xs);
      fputc(')', out);
      break;
    }
    fputs("ov_method_", out);
    fputs(n->s, out);
    fputc('(', out);
    emit_expression(out, n->a);
    if (n->xs.n) {
      fputc(',', out);
      emit_args(out, &n->xs);
    } else {
      fputs(",0,NULL", out);
    }
    fputc(')', out);
    break;
  case N_IF:
    fputs("(ov_truthy(", out);
    emit_expression(out, n->a);
    fputs(")?", out);
    emit_expression(out, n->b);
    fputc(':', out);
    emit_expression(out, n->c);
    fputc(')', out);
    break;
  case N_MATCH:
    fputs("({ OValue _m=", out);
    emit_expression(out, n->a);
    fputs("; OValue _r=ov_unit();", out);
    for (size_t i = 0; i < n->arms.n; i++) {
      Arm *arm = &n->arms.v[i];
      if (!strcmp(arm->ctor, "_")) {
        fputs(i ? "else{" : "{", out);
      } else {
        fprintf(out, "%sif(ov_is_ctor(_m,", i ? "else " : "");
        emit_string(out, arm->ctor);
        fputs(")){", out);
      }
      if (arm->bind) {
        fputs("OValue ", out);
        emit_identifier(out, arm->bind);
        fputs("=ov_payload(_m);", out);
      }
      fputs("_r=", out);
      emit_expression(out, arm->body);
      fputs(";}", out);
    }
    if (strcmp(n->arms.v[n->arms.n - 1].ctor, "_")) {
      fputs("else ov_panic(\"non-exhaustive match\");", out);
    }
    fputs(" _r; })", out);
    break;
  }
}

void generate_c(FILE *out, Stmts *program, const char *source) {
  char **visible = NULL;
  size_t visible_count = 0;
  for (size_t i = 0; i < program->n; i++) {
    Stmt *s = &program->v[i];
    if (s->kind == S_FN) {
      discover(s->expr, s->params, s->nparams);
    } else {
      discover(s->expr, visible, visible_count);
      if (s->kind == S_LET) {
        visible = realloc(visible, (visible_count + 1) * sizeof(*visible));
        if (!visible) {
          die("out of memory");
        }
        visible[visible_count++] = s->name;
      }
    }
  }
  fprintf(out, "/* Generated by Oryn-ML from %s. */\n#include \"oryn/runtime.h\"\n", source);
  for (size_t i = 0; i < program->n; i++) {
    if (program->v[i].kind == S_FN) {
      fputs("static OValue ", out);
      emit_identifier(out, program->v[i].name);
      fputs("(size_t,OValue*);\n", out);
    }
  }
  for (size_t i = 0; i < lambda_count; i++) {
    fprintf(out, "static OValue oryn_lambda_%zu(size_t,OValue*,OValue*);\n", i);
  }
  for (size_t i = 0; i < program->n; i++) {
    Stmt *stmt = &program->v[i];
    if (stmt->kind != S_FN) {
      continue;
    }
    fputs("static OValue ", out);
    emit_identifier(out, stmt->name);
    fprintf(out,
            "(size_t _argc,OValue*_argv){if(_argc!=%zu)ov_panic(\"wrong argument count for %s\");",
            stmt->nparams,
            stmt->name);
    for (size_t j = 0; j < stmt->nparams; j++) {
      fputs("OValue ", out);
      emit_identifier(out, stmt->params[j]);
      fprintf(out, "=_argv[%zu];", j);
      fputs("ov_gc_push(&", out);
      emit_identifier(out, stmt->params[j]);
      fputs(");", out);
    }
    fputs("OValue _result=", out);
    emit_expression(out, stmt->expr);
    fprintf(out, ";ov_gc_pop(%zu);return _result;}\n", stmt->nparams);
  }
  for (size_t i = 0; i < lambda_count; i++) {
    Node *lambda = lambdas[i];
    current_lambda = lambda;
    fprintf(out,
            "static OValue oryn_lambda_%zu(size_t "
            "_argc,OValue*_argv,OValue*_env){if(_argc!=%zu)ov_panic(\"wrong anonymous function "
            "argument count\");",
            i,
            lambda->nparams);
    for (size_t j = 0; j < lambda->nparams; j++) {
      fputs("OValue ", out);
      emit_identifier(out, lambda->params[j]);
      fprintf(out, "=_argv[%zu];", j);
    }
    fputs("return ", out);
    emit_expression(out, lambda->a);
    fputs(";}\n", out);
    current_lambda = NULL;
  }
  fputs("int main(void){\nov_gc_init();OValue oryn_None=ov_none();ov_gc_push(&oryn_None);\n", out);
  size_t roots = 1;
  for (size_t i = 0; i < program->n; i++) {
    Stmt *stmt = &program->v[i];
    if (stmt->kind == S_FN) {
      continue;
    }
    if (stmt->kind == S_LET) {
      fputs("OValue ", out);
      emit_identifier(out, stmt->name);
      fputc('=', out);
      emit_expression(out, stmt->expr);
      fputs(";ov_gc_push(&", out);
      emit_identifier(out, stmt->name);
      fputs(");\n", out);
      roots++;
    } else {
      fputs("(void)", out);
      emit_expression(out, stmt->expr);
      fputs(";\n", out);
    }
    fputs("ov_gc_collect();\n", out);
  }
  fprintf(out, "ov_gc_pop(%zu);ov_gc_shutdown();return 0;}\n", roots);
}
