#include "oryn/compiler/sema.h"

#include "oryn/compiler/base.h"
#include "oryn/compiler/token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  K_UNKNOWN,
  K_UNIT,
  K_INT,
  K_FLOAT,
  K_BOOL,
  K_STRING,
  K_ARRAY,
  K_OPTION,
  K_RESULT,
  K_FUNCTION
} Kind;
typedef struct Type Type;

struct Type {
  Kind kind;
  Type *a, *b;
};

typedef struct Symbol Symbol;

struct Symbol {
  const char *name;
  Type *type;
  size_t arity;
  Type **params;
  int function;
  Symbol *next;
};

static const char *path;
static const char *source;
static Type unknown_type = {.kind = K_UNKNOWN}, unit_type = {.kind = K_UNIT},
            int_type = {.kind = K_INT}, float_type = {.kind = K_FLOAT};
static Type bool_type = {.kind = K_BOOL}, string_type = {.kind = K_STRING};
#define UNKNOWN (&unknown_type)
#define UNIT (&unit_type)
#define INT (&int_type)
#define FLOAT (&float_type)
#define BOOL (&bool_type)
#define STRING (&string_type)

static Type *compound(Kind kind, Type *a, Type *b) {
  Type *t = xmalloc(sizeof(*t));
  *t = (Type){kind, a, b};
  return t;
}

static void error(Node *n, const char *message, const char *name) {
  int line = n ? n->line : 1, col = n ? n->col : 1;
  fprintf(stderr,
          "oryn: %s:%d:%d: semantic error: %s%s%s%s\n",
          path,
          line,
          col,
          message,
          name ? " '" : "",
          name ? name : "",
          name ? "'" : "");
  const char *start = source;
  for (int current = 1; current < line && *start; current++) {
    const char *newline = strchr(start, '\n');
    start = newline ? newline + 1 : start + strlen(start);
  }
  const char *end = strchr(start, '\n');
  if (!end) {
    end = start + strlen(start);
  }
  fprintf(stderr, "  %.*s\n  %*s^\n", (int)(end - start), start, col > 1 ? col - 1 : 0, "");
  exit(1);
}

static int is(Type *t, Kind k) {
  return t && t->kind == k;
}

static int compatible(Type *x, Type *y) {
  if (is(x, K_UNKNOWN) || is(y, K_UNKNOWN)) {
    return 1;
  }
  if (x->kind != y->kind) {
    return (is(x, K_INT) || is(x, K_FLOAT)) && (is(y, K_INT) || is(y, K_FLOAT));
  }
  if (is(x, K_ARRAY) || is(x, K_OPTION)) {
    return compatible(x->a, y->a);
  }
  if (is(x, K_RESULT)) {
    return compatible(x->a, y->a) && compatible(x->b, y->b);
  }
  return 1;
}

static Type *merge(Type *x, Type *y) {
  if (is(x, K_UNKNOWN)) {
    return y;
  }
  if (is(y, K_UNKNOWN)) {
    return x;
  }
  if (!compatible(x, y)) {
    return NULL;
  }
  if ((is(x, K_INT) && is(y, K_FLOAT)) || (is(x, K_FLOAT) && is(y, K_INT))) {
    return FLOAT;
  }
  if (is(x, K_ARRAY)) {
    return compound(K_ARRAY, merge(x->a, y->a), NULL);
  }
  if (is(x, K_OPTION)) {
    return compound(K_OPTION, merge(x->a, y->a), NULL);
  }
  if (is(x, K_RESULT)) {
    return compound(K_RESULT, merge(x->a, y->a), merge(x->b, y->b));
  }
  return x;
}

static Type *parse_type_at(const char **at) {
  const char *s = *at, *start = s;
  while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_') {
    s++;
  }
  size_t n = (size_t)(s - start);
  Kind k = K_UNKNOWN;
#define NAME(v, kind)                                                                              \
  if (n == strlen(v) && !strncmp(start, v, n))                                                     \
  k = kind
  NAME("Unit", K_UNIT);
  else NAME("Int", K_INT);
  else NAME("Float", K_FLOAT);
  else NAME("Bool", K_BOOL);
  else NAME("String", K_STRING);
  else NAME("Array", K_ARRAY);
  else NAME("Option", K_OPTION);
  else NAME("Result", K_RESULT);
#undef NAME
  if (k == K_UNKNOWN) {
    die("%s: unknown type '%.*s'", path, (int)n, start);
  }
  Type *a = NULL, *b = NULL;
  if (*s == '<') {
    s++;
    *at = s;
    a = parse_type_at(at);
    s = *at;
    if (*s == ',') {
      s++;
      *at = s;
      b = parse_type_at(at);
      s = *at;
    }
    if (*s != '>') {
      die("%s: malformed generic type", path);
    }
    s++;
  }
  *at = s;
  if (k == K_ARRAY || k == K_OPTION) {
    if (!a) {
      die("%s: generic type requires one argument", path);
    }
    return compound(k, a, NULL);
  }
  if (k == K_RESULT) {
    if (!a || !b) {
      die("%s: Result requires two type arguments", path);
    }
    return compound(k, a, b);
  }
  if (a) {
    die("%s: primitive type cannot have arguments", path);
  }
  return k == K_UNIT ? UNIT : k == K_INT ? INT : k == K_FLOAT ? FLOAT : k == K_BOOL ? BOOL : STRING;
}

static Type *annotation(const char *s) {
  if (!s) {
    return UNKNOWN;
  }
  const char *p = s;
  Type *t = parse_type_at(&p);
  if (*p) {
    die("%s: malformed type '%s'", path, s);
  }
  return t;
}

static Symbol *find(Symbol *s, const char *n) {
  for (; s; s = s->next) {
    if (!strcmp(s->name, n)) {
      return s;
    }
  }
  return NULL;
}

static Symbol *
add(Symbol *s, const char *n, Type *t, size_t arity, Type **params, int fn, int line) {
  if (find(s, n)) {
    die("%s:%d: semantic error: duplicate declaration '%s'", path, line, n);
  }
  Symbol *x = xmalloc(sizeof(*x));
  *x = (Symbol){n, t, arity, params, fn, s};
  return x;
}

static Type *expr(Node *n, Symbol *scope);

static Type *call(Node *n, Symbol *scope) {
  if (n->a->kind != N_VAR) {
    error(n, "only named functions can be called", NULL);
  }
  const char *name = n->a->s;
  if (!strcmp(name, "Some")) {
    if (n->xs.n != 1) {
      error(n, "Some expects one argument", NULL);
    }
    return compound(K_OPTION, expr(n->xs.v[0], scope), NULL);
  }
  if (!strcmp(name, "Ok") || !strcmp(name, "Err")) {
    if (n->xs.n != 1) {
      error(n, "constructor expects one argument", NULL);
    }
    Type *t = expr(n->xs.v[0], scope);
    return !strcmp(name, "Ok") ? compound(K_RESULT, t, UNKNOWN) : compound(K_RESULT, UNKNOWN, t);
  }
  Symbol *s = find(scope, name);
  if (!s || !s->function) {
    error(n, "undefined function", name);
  }
  if (s->arity != (size_t)-1 && s->arity != n->xs.n) {
    die("%s:%d: semantic error: '%s' expects %zu argument(s), got %zu",
        path,
        n->line,
        name,
        s->arity,
        n->xs.n);
  }
  for (size_t i = 0; i < n->xs.n; i++) {
    Type *t = expr(n->xs.v[i], scope);
    if (s->params && !compatible(t, s->params[i])) {
      die("%s:%d: semantic error: incompatible argument %zu of '%s'", path, n->line, i + 1, name);
    }
  }
  return s->type;
}

static Type *method(Node *n, Symbol *scope) {
  if (n->a->kind == N_VAR && (!strcmp(n->a->s, "Int") || !strcmp(n->a->s, "Float")) &&
      !strcmp(n->s, "parse")) {
    if (n->xs.n != 1 || !compatible(expr(n->xs.v[0], scope), STRING)) {
      error(n, "parse expects one String", NULL);
    }
    return compound(K_RESULT, !strcmp(n->a->s, "Int") ? INT : FLOAT, STRING);
  }
  Type *r = expr(n->a, scope);
  Type *args[2] = {UNKNOWN, UNKNOWN};
  for (size_t i = 0; i < n->xs.n && i < 2; i++) {
    args[i] = expr(n->xs.v[i], scope);
  }
  const char *m = n->s;
  size_t arity = n->xs.n;
#define ARITY(v, a)                                                                                \
  if (!strcmp(m, v) && arity != (a))                                                               \
  error(n, v " received the wrong number of arguments", NULL)
  ARITY("len", 0);
  ARITY("is_empty", 0);
  ARITY("first", 0);
  ARITY("last", 0);
  ARITY("trim", 0);
  ARITY("to_lower", 0);
  ARITY("to_upper", 0);
  ARITY("reverse", 0);
  ARITY("is_ok", 0);
  ARITY("is_err", 0);
  ARITY("get", 1);
  ARITY("contains", 1);
  ARITY("starts_with", 1);
  ARITY("ends_with", 1);
  ARITY("split", 1);
  ARITY("concat", 1);
  ARITY("unwrap_or", 1);
  ARITY("slice", 2);
  ARITY("map", 1);
  ARITY("filter", 1);
  ARITY("fold", 2);
#undef ARITY
  if (!strcmp(m, "len") || !strcmp(m, "is_empty")) {
    if (!is(r, K_ARRAY) && !is(r, K_STRING) && !is(r, K_UNKNOWN)) {
      error(n, "method requires Array or String", m);
    }
    return !strcmp(m, "len") ? INT : BOOL;
  }
  if (!strcmp(m, "get") || !strcmp(m, "first") || !strcmp(m, "last")) {
    if (!is(r, K_ARRAY) && !is(r, K_UNKNOWN)) {
      error(n, "method requires Array", m);
    }
    if (!strcmp(m, "get") && !compatible(args[0], INT)) {
      error(n, "get index must be Int", NULL);
    }
    return compound(K_OPTION, is(r, K_ARRAY) ? r->a : UNKNOWN, NULL);
  }
  if (!strcmp(m, "trim") || !strcmp(m, "to_lower") || !strcmp(m, "to_upper")) {
    if (!compatible(r, STRING)) {
      error(n, "method requires String", m);
    }
    return STRING;
  }
  if (!strcmp(m, "starts_with") || !strcmp(m, "ends_with")) {
    if (!compatible(r, STRING) || !compatible(args[0], STRING)) {
      error(n, "method requires strings", m);
    }
    return BOOL;
  }
  if (!strcmp(m, "split")) {
    if (!compatible(r, STRING) || !compatible(args[0], STRING)) {
      error(n, "split requires strings", NULL);
    }
    return compound(K_ARRAY, STRING, NULL);
  }
  if (!strcmp(m, "contains")) {
    if (is(r, K_STRING) && !compatible(args[0], STRING)) {
      error(n, "String.contains requires String", NULL);
    }
    if (is(r, K_ARRAY) && !compatible(args[0], r->a)) {
      error(n, "Array.contains element type differs", NULL);
    }
    if (!is(r, K_STRING) && !is(r, K_ARRAY) && !is(r, K_UNKNOWN)) {
      error(n, "contains requires Array or String", NULL);
    }
    return BOOL;
  }
  if (!strcmp(m, "concat")) {
    if (!is(r, K_ARRAY) || !is(args[0], K_ARRAY) || !compatible(r, args[0])) {
      error(n, "concat requires compatible arrays", NULL);
    }
    return merge(r, args[0]);
  }
  if (!strcmp(m, "reverse")) {
    if (!is(r, K_ARRAY)) {
      error(n, "reverse requires Array", NULL);
    }
    return r;
  }
  if (!strcmp(m, "slice")) {
    if (!is(r, K_ARRAY) || !compatible(args[0], INT) || !compatible(args[1], INT)) {
      error(n, "slice requires Array and Int indexes", NULL);
    }
    return compound(K_RESULT, r, STRING);
  }
  if (!strcmp(m, "unwrap_or")) {
    if (is(r, K_OPTION)) {
      if (!compatible(r->a, args[0])) {
        error(n, "fallback type differs from Option payload", NULL);
      }
      return merge(r->a, args[0]);
    }
    if (is(r, K_RESULT)) {
      if (!compatible(r->a, args[0])) {
        error(n, "fallback type differs from Result value", NULL);
      }
      return merge(r->a, args[0]);
    }
    error(n, "unwrap_or requires Option or Result", NULL);
  }
  if (!strcmp(m, "is_ok") || !strcmp(m, "is_err")) {
    if (!is(r, K_RESULT)) {
      error(n, "method requires Result", m);
    }
    return BOOL;
  }
  if (!strcmp(m, "map") || !strcmp(m, "filter") || !strcmp(m, "fold")) {
    if (!is(r, K_ARRAY)) {
      error(n, "method requires Array", m);
    }
    size_t function_index = !strcmp(m, "fold") ? 1 : 0;
    if (!is(args[function_index], K_FUNCTION)) {
      error(n, "method requires an anonymous function", m);
    }
    if (!strcmp(m, "filter")) {
      if (!compatible(args[0]->a, BOOL)) {
        error(n, "filter function must return Bool", NULL);
      }
      return r;
    }
    if (!strcmp(m, "map")) {
      return compound(K_ARRAY, args[0]->a, NULL);
    }
    if (!compatible(args[1]->a, args[0])) {
      error(n, "fold function result differs from accumulator", NULL);
    }
    return merge(args[0], args[1]->a);
  }
  error(n, "unknown method", m);
  return UNKNOWN;
}

static Type *expr(Node *n, Symbol *scope) {
  switch (n->kind) {
  case N_INT:
    return INT;
  case N_FLOAT:
    return FLOAT;
  case N_STRING:
    return STRING;
  case N_BOOL:
    return BOOL;
  case N_VAR: {
    Symbol *s = find(scope, n->s);
    if (!s) {
      error(n, "undefined name", n->s);
    }
    return s->type;
  }
  case N_ARRAY: {
    Type *element = UNKNOWN;
    for (size_t i = 0; i < n->xs.n; i++) {
      Type *next = expr(n->xs.v[i], scope);
      element = merge(element, next);
      if (!element) {
        error(n, "array elements have incompatible types", NULL);
      }
    }
    return compound(K_ARRAY, element, NULL);
  }
  case N_LAMBDA: {
    Symbol *local = scope;
    for (size_t i = 0; i < n->nparams; i++) {
      local = add(local, n->params[i], annotation(n->param_types[i]), 0, NULL, 0, n->line);
    }
    return compound(K_FUNCTION, expr(n->a, local), NULL);
  }
  case N_CALL:
    return call(n, scope);
  case N_METHOD:
    return method(n, scope);
  case N_UNARY: {
    Type *t = expr(n->a, scope);
    if (n->op == T_BANG) {
      if (!compatible(t, BOOL)) {
        error(n, "! requires Bool", NULL);
      }
      return BOOL;
    }
    if (!is(t, K_INT) && !is(t, K_FLOAT) && !is(t, K_UNKNOWN)) {
      error(n, "unary - requires a number", NULL);
    }
    return t;
  }
  case N_BINARY: {
    Type *a = expr(n->a, scope), *b = expr(n->b, scope);
    int op = n->op;
    if (op == T_AND || op == T_OR) {
      if (!compatible(a, BOOL) || !compatible(b, BOOL)) {
        error(n, "boolean operator requires Bool", NULL);
      }
      return BOOL;
    }
    if (op == T_EQEQ || op == T_NE) {
      if (!compatible(a, b)) {
        error(n, "equality operands differ", NULL);
      }
      return BOOL;
    }
    if (op == T_LT || op == T_LE || op == T_GT || op == T_GE) {
      if ((!is(a, K_INT) && !is(a, K_FLOAT)) || (!is(b, K_INT) && !is(b, K_FLOAT))) {
        error(n, "ordering requires numbers", NULL);
      }
      return BOOL;
    }
    if (op == T_PLUS && is(a, K_STRING) && is(b, K_STRING)) {
      return STRING;
    }
    if ((!is(a, K_INT) && !is(a, K_FLOAT)) || (!is(b, K_INT) && !is(b, K_FLOAT))) {
      error(n, "arithmetic requires numbers", NULL);
    }
    return merge(a, b);
  }
  case N_IF: {
    if (!compatible(expr(n->a, scope), BOOL)) {
      error(n, "if condition must be Bool", NULL);
    }
    Type *t = merge(expr(n->b, scope), expr(n->c, scope));
    if (!t) {
      error(n, "if branches have incompatible types", NULL);
    }
    return t;
  }
  case N_MATCH: {
    Type *subject = expr(n->a, scope), *result = UNKNOWN;
    if (!n->arms.n) {
      error(n, "match must contain at least one arm", NULL);
    }
    int some = 0, none = 0, ok = 0, err = 0, wildcard = 0;
    for (size_t i = 0; i < n->arms.n; i++) {
      Arm *a = &n->arms.v[i];
      for (size_t j = 0; j < i; j++) {
        if (!strcmp(a->ctor, n->arms.v[j].ctor)) {
          error(n, "duplicate match arm", a->ctor);
        }
      }
      Type *payload = UNKNOWN;
      if (!strcmp(a->ctor, "_")) {
        wildcard = 1;
        if (i + 1 != n->arms.n) {
          error(n, "wildcard match arm must be last", NULL);
        }
      } else if (!strcmp(a->ctor, "Some")) {
        some = 1;
        if (is(subject, K_OPTION)) {
          payload = subject->a;
        }
      } else if (!strcmp(a->ctor, "None")) {
        none = 1;
      } else if (!strcmp(a->ctor, "Ok")) {
        ok = 1;
        if (is(subject, K_RESULT)) {
          payload = subject->a;
        }
      } else if (!strcmp(a->ctor, "Err")) {
        err = 1;
        if (is(subject, K_RESULT)) {
          payload = subject->b;
        }
      } else {
        error(n, "unknown constructor", a->ctor);
      }
      Symbol local = {a->bind ? a->bind : "", payload, 0, NULL, 0, scope};
      Type *t = expr(a->body, a->bind ? &local : scope);
      result = merge(result, t);
      if (!result) {
        error(n, "match arms have incompatible types", NULL);
      }
    }
    if (!wildcard && (is(subject, K_OPTION) || some || none) && !(some && none)) {
      error(n, "non-exhaustive Option match", NULL);
    }
    if (!wildcard && (is(subject, K_RESULT) || ok || err) && !(ok && err)) {
      error(n, "non-exhaustive Result match", NULL);
    }
    return result;
  }
  }
  return UNKNOWN;
}

void analyze_program(Stmts *program, const char *source_path, const char *source_text) {
  path = source_path;
  source = source_text;
  Symbol *global = NULL;

  const struct {
    const char *n;
    size_t arity;
    Type *result;
    Type *p0, *p1;
  } b[] = {{"print", (size_t)-1, UNIT, NULL, NULL},
           {"println", (size_t)-1, UNIT, NULL, NULL},
           {"assert", 1, UNIT, BOOL, NULL},
           {"panic", 1, UNIT, STRING, NULL},
           {"len", 1, INT, UNKNOWN, NULL},
           {"get", 2, compound(K_OPTION, UNKNOWN, NULL), compound(K_ARRAY, UNKNOWN, NULL), INT},
           {"parse_int", 1, compound(K_RESULT, INT, STRING), STRING, NULL},
           {"parse_float", 1, compound(K_RESULT, FLOAT, STRING), STRING, NULL},
           {"to_string", 1, STRING, UNKNOWN, NULL}};

  for (size_t i = 0; i < sizeof b / sizeof *b; i++) {
    Type **p = NULL;
    if (b[i].arity != (size_t)-1) {
      p = xmalloc(b[i].arity * sizeof(*p));
      p[0] = b[i].p0;
      if (b[i].arity > 1) {
        p[1] = b[i].p1;
      }
    }
    global = add(global, b[i].n, b[i].result, b[i].arity, p, 1, 1);
  }
  global = add(global, "None", compound(K_OPTION, UNKNOWN, NULL), 0, NULL, 0, 1);
  for (size_t i = 0; i < program->n; i++) {
    if (program->v[i].kind == S_FN) {
      Stmt *s = &program->v[i];
      Type **p = s->nparams ? xmalloc(s->nparams * sizeof(*p)) : NULL;
      for (size_t j = 0; j < s->nparams; j++) {
        p[j] = annotation(s->param_types[j]);
      }
      global = add(global, s->name, annotation(s->type), s->nparams, p, 1, s->line);
    }
  }
  for (size_t i = 0; i < program->n; i++) {
    Stmt *s = &program->v[i];
    if (s->kind == S_FN) {
      Symbol *local = global;
      for (size_t j = 0; j < s->nparams; j++) {
        local = add(local, s->params[j], annotation(s->param_types[j]), 0, NULL, 0, s->line);
      }
      if (!compatible(expr(s->expr, local), annotation(s->type))) {
        error(s->expr, "function return type is incompatible", s->name);
      }
    } else if (s->kind == S_LET) {
      Type *got = expr(s->expr, global), *want = annotation(s->type);
      if (!compatible(got, want)) {
        error(s->expr, "binding annotation is incompatible", s->name);
      }
      global = add(global, s->name, is(want, K_UNKNOWN) ? got : want, 0, NULL, 0, s->line);
    } else {
      expr(s->expr, global);
    }
  }
}
