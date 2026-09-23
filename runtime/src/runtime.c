#include "oryn/runtime.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GCBlock GCBlock;

struct GCBlock {
  GCBlock *next;
  size_t size;
  int marked;
};

static GCBlock *gc_blocks;
static OValue **gc_roots;
static size_t gc_root_count, gc_root_cap;
static size_t gc_bytes, gc_objects, gc_collections;

void ov_gc_init(void) {
  gc_blocks = NULL;
  gc_root_count = 0;
  gc_bytes = gc_objects = gc_collections = 0;
}

void ov_gc_push(OValue *root) {
  if (gc_root_count == gc_root_cap) {
    gc_root_cap = gc_root_cap ? gc_root_cap * 2 : 32;
    OValue **next = realloc(gc_roots, gc_root_cap * sizeof(*next));
    if (!next) {
      ov_panic("out of memory");
    }
    gc_roots = next;
  }
  gc_roots[gc_root_count++] = root;
}

void ov_gc_pop(size_t count) {
  if (count > gc_root_count) {
    ov_panic("invalid GC root stack");
  }
  gc_root_count -= count;
}

void ov_panic(const char *message) {
  fprintf(stderr, "panic: %s\n", message);
  exit(1);
}

static void *ov_alloc(size_t size) {
  GCBlock *block = calloc(1, sizeof(*block) + size);
  if (!block) {
    ov_panic("out of memory");
  }
  block->size = size;
  block->next = gc_blocks;
  gc_blocks = block;
  gc_bytes += size;
  gc_objects++;
  return block + 1;
}

static GCBlock *gc_find(void *ptr) {
  if (!ptr) {
    return NULL;
  }
  GCBlock *wanted = ((GCBlock *)ptr) - 1;
  for (GCBlock *b = gc_blocks; b; b = b->next) {
    if (b == wanted) {
      return b;
    }
  }
  return NULL;
}

static void gc_mark_ptr(void *ptr) {
  GCBlock *b = gc_find(ptr);
  if (b) {
    b->marked = 1;
  }
}

static void gc_mark_value(OValue v) {
  if (v.tag == OV_STRING) {
    gc_mark_ptr(v.as.s);
  } else if (v.tag == OV_ARRAY && v.as.a) {
    GCBlock *b = gc_find(v.as.a);
    if (b && !b->marked) {
      b->marked = 1;
      gc_mark_ptr(v.as.a->data);
      for (size_t i = 0; i < v.as.a->len; i++) {
        gc_mark_value(v.as.a->data[i]);
      }
    }
  } else if ((v.tag == OV_OPTION || v.tag == OV_RESULT) && v.as.sum.value) {
    GCBlock *b = gc_find(v.as.sum.value);
    if (b && !b->marked) {
      b->marked = 1;
      gc_mark_value(*v.as.sum.value);
    }
  } else if (v.tag == OV_FUNCTION && v.as.fn) {
    GCBlock *b = gc_find(v.as.fn);
    if (b && !b->marked) {
      b->marked = 1;
      gc_mark_ptr(v.as.fn->env);
      for (size_t i = 0; i < v.as.fn->env_len; i++) {
        gc_mark_value(v.as.fn->env[i]);
      }
    }
  }
}

void ov_gc_collect(void) {
  gc_collections++;
  for (size_t i = 0; i < gc_root_count; i++) {
    gc_mark_value(*gc_roots[i]);
  }
  GCBlock **at = &gc_blocks;
  while (*at) {
    GCBlock *b = *at;
    if (!b->marked) {
      *at = b->next;
      gc_bytes -= b->size;
      gc_objects--;
      free(b);
    } else {
      b->marked = 0;
      at = &b->next;
    }
  }
}

size_t ov_gc_live_bytes(void) {
  return gc_bytes;
}

size_t ov_gc_live_objects(void) {
  return gc_objects;
}

size_t ov_gc_collection_count(void) {
  return gc_collections;
}

void ov_gc_shutdown(void) {
  GCBlock *b = gc_blocks;
  while (b) {
    GCBlock *next = b->next;
    free(b);
    b = next;
  }
  gc_blocks = NULL;
  free(gc_roots);
  gc_roots = NULL;
  gc_root_count = gc_root_cap = 0;
}

static char *ov_dup(const char *s) {
  size_t n = strlen(s) + 1;
  char *copy = ov_alloc(n);
  memcpy(copy, s, n);
  return copy;
}

OValue ov_unit(void) {
  return (OValue){.tag = OV_UNIT};
}

OValue ov_int(long x) {
  return (OValue){.tag = OV_INT, .as.i = x};
}

OValue ov_float(double x) {
  return (OValue){.tag = OV_FLOAT, .as.f = x};
}

OValue ov_bool(int x) {
  return (OValue){.tag = OV_BOOL, .as.b = !!x};
}

OValue ov_string(const char *x) {
  OValue v = {.tag = OV_STRING};
  v.as.s = ov_dup(x);
  return v;
}

static double ov_num(OValue v) {
  if (v.tag == OV_INT) {
    return (double)v.as.i;
  }
  if (v.tag == OV_FLOAT) {
    return v.as.f;
  }
  ov_panic("expected number");
  return 0;
}

int ov_truthy(OValue v) {
  if (v.tag == OV_BOOL) {
    return v.as.b;
  }
  if (v.tag == OV_INT) {
    return v.as.i != 0;
  }
  ov_panic("expected Bool");
  return 0;
}

OValue ov_add(OValue a, OValue b) {
  if (a.tag == OV_STRING && b.tag == OV_STRING) {
    size_t n = strlen(a.as.s) + strlen(b.as.s) + 1;
    char *s = ov_alloc(n);
    strcpy(s, a.as.s);
    strcat(s, b.as.s);
    OValue v = {.tag = OV_STRING, .as.s = s};
    return v;
  }
  if (a.tag == OV_INT && b.tag == OV_INT) {
    return ov_int(a.as.i + b.as.i);
  }
  return ov_float(ov_num(a) + ov_num(b));
}

OValue ov_sub(OValue a, OValue b) {
  if (a.tag == OV_INT && b.tag == OV_INT) {
    return ov_int(a.as.i - b.as.i);
  }
  return ov_float(ov_num(a) - ov_num(b));
}

OValue ov_mul(OValue a, OValue b) {
  if (a.tag == OV_INT && b.tag == OV_INT) {
    return ov_int(a.as.i * b.as.i);
  }
  return ov_float(ov_num(a) * ov_num(b));
}

OValue ov_div(OValue a, OValue b) {
  double d = ov_num(b);
  if (d == 0) {
    ov_panic("division by zero");
  }
  if (a.tag == OV_INT && b.tag == OV_INT) {
    return ov_int(a.as.i / b.as.i);
  }
  return ov_float(ov_num(a) / d);
}

OValue ov_mod(OValue a, OValue b) {
  if (a.tag != OV_INT || b.tag != OV_INT || b.as.i == 0) {
    ov_panic("invalid remainder");
  }
  return ov_int(a.as.i % b.as.i);
}

static int ov_equal(OValue a, OValue b) {
  if (a.tag != b.tag) {
    if ((a.tag == OV_INT || a.tag == OV_FLOAT) && (b.tag == OV_INT || b.tag == OV_FLOAT)) {
      return ov_num(a) == ov_num(b);
    }
    return 0;
  }
  switch (a.tag) {
  case OV_UNIT:
    return 1;
  case OV_INT:
    return a.as.i == b.as.i;
  case OV_FLOAT:
    return a.as.f == b.as.f;
  case OV_BOOL:
    return a.as.b == b.as.b;
  case OV_STRING:
    return !strcmp(a.as.s, b.as.s);
  case OV_ARRAY:
    if (a.as.a->len != b.as.a->len) {
      return 0;
    }
    for (size_t i = 0; i < a.as.a->len; i++) {
      if (!ov_equal(a.as.a->data[i], b.as.a->data[i])) {
        return 0;
      }
    }
    return 1;
  default:
    return a.as.sum.kind == b.as.sum.kind;
  }
}

OValue ov_eq(OValue a, OValue b) {
  return ov_bool(ov_equal(a, b));
}

OValue ov_ne(OValue a, OValue b) {
  return ov_bool(!ov_equal(a, b));
}

OValue ov_lt(OValue a, OValue b) {
  return ov_bool(ov_num(a) < ov_num(b));
}

OValue ov_le(OValue a, OValue b) {
  return ov_bool(ov_num(a) <= ov_num(b));
}

OValue ov_gt(OValue a, OValue b) {
  return ov_bool(ov_num(a) > ov_num(b));
}

OValue ov_ge(OValue a, OValue b) {
  return ov_bool(ov_num(a) >= ov_num(b));
}

OValue ov_and(OValue a, OValue b) {
  return ov_bool(ov_truthy(a) && ov_truthy(b));
}

OValue ov_or(OValue a, OValue b) {
  return ov_bool(ov_truthy(a) || ov_truthy(b));
}

OValue ov_neg(OValue a) {
  return a.tag == OV_INT ? ov_int(-a.as.i) : ov_float(-ov_num(a));
}

OValue ov_not(OValue a) {
  return ov_bool(!ov_truthy(a));
}

OValue ov_array(size_t n, OValue *values) {
  OArray *a = ov_alloc(sizeof(*a));
  a->len = n;
  a->data = ov_alloc((n ? n : 1) * sizeof(OValue));
  if (n) {
    memcpy(a->data, values, n * sizeof(OValue));
  }
  OValue v = {.tag = OV_ARRAY, .as.a = a};
  return v;
}

static OValue ov_sum(OTag tag, int kind, OValue x) {
  OValue v = {.tag = tag};
  v.as.sum.kind = kind;
  v.as.sum.value = ov_alloc(sizeof(OValue));
  *v.as.sum.value = x;
  return v;
}

OValue ov_some(OValue x) {
  return ov_sum(OV_OPTION, 1, x);
}

OValue ov_none(void) {
  return (OValue){.tag = OV_OPTION};
}

OValue ov_ok(OValue x) {
  return ov_sum(OV_RESULT, 1, x);
}

OValue ov_err(OValue x) {
  return ov_sum(OV_RESULT, 2, x);
}

OValue ov_function(OFunctionBody body, size_t n, OValue *env) {
  OFunction *fn = ov_alloc(sizeof(*fn));
  fn->body = body;
  fn->env_len = n;
  fn->env = ov_alloc((n ? n : 1) * sizeof(*fn->env));
  if (n) {
    memcpy(fn->env, env, n * sizeof(*env));
  }
  return (OValue){.tag = OV_FUNCTION, .as.fn = fn};
}

OValue ov_call(OValue fn, size_t n, OValue *args) {
  if (fn.tag != OV_FUNCTION) {
    ov_panic("attempted to call a non-function");
  }
  return fn.as.fn->body(n, args, fn.as.fn->env);
}

int ov_is_ctor(OValue v, const char *c) {
  return (!strcmp(c, "Some") && v.tag == OV_OPTION && v.as.sum.kind == 1) ||
         (!strcmp(c, "None") && v.tag == OV_OPTION && !v.as.sum.kind) ||
         (!strcmp(c, "Ok") && v.tag == OV_RESULT && v.as.sum.kind == 1) ||
         (!strcmp(c, "Err") && v.tag == OV_RESULT && v.as.sum.kind == 2);
}

OValue ov_payload(OValue v) {
  if (!v.as.sum.value) {
    ov_panic("constructor has no payload");
  }
  return *v.as.sum.value;
}

static void ov_show(OValue v) {
  switch (v.tag) {
  case OV_UNIT:
    printf("()");
    break;
  case OV_INT:
    printf("%ld", v.as.i);
    break;
  case OV_FLOAT:
    printf("%g", v.as.f);
    break;
  case OV_BOOL:
    printf(v.as.b ? "true" : "false");
    break;
  case OV_STRING:
    printf("%s", v.as.s);
    break;
  case OV_ARRAY:
    putchar('[');
    for (size_t i = 0; i < v.as.a->len; i++) {
      if (i) {
        printf(", ");
      }
      ov_show(v.as.a->data[i]);
    }
    putchar(']');
    break;
  case OV_OPTION:
    if (!v.as.sum.kind) {
      printf("None");
    } else {
      printf("Some(");
      ov_show(*v.as.sum.value);
      putchar(')');
    }
    break;
  case OV_RESULT:
    printf(v.as.sum.kind == 1 ? "Ok(" : "Err(");
    ov_show(*v.as.sum.value);
    putchar(')');
    break;
  case OV_FUNCTION:
    printf("<function>");
    break;
  }
}

OValue oryn_print(size_t n, OValue *a) {
  for (size_t i = 0; i < n; i++) {
    ov_show(a[i]);
  }
  return ov_unit();
}

OValue oryn_println(size_t n, OValue *a) {
  oryn_print(n, a);
  putchar('\n');
  return ov_unit();
}

OValue oryn_assert(size_t n, OValue *a) {
  if (n != 1 || !ov_truthy(a[0])) {
    ov_panic("assertion failed");
  }
  return ov_unit();
}

OValue oryn_panic(size_t n, OValue *a) {
  if (n != 1 || a[0].tag != OV_STRING) {
    ov_panic("panic expects String");
  }
  ov_panic(a[0].as.s);
  return ov_unit();
}

OValue oryn_len(size_t n, OValue *a) {
  if (n != 1) {
    ov_panic("len expects one value");
  }
  if (a[0].tag == OV_ARRAY) {
    return ov_int((long)a[0].as.a->len);
  }
  if (a[0].tag == OV_STRING) {
    return ov_int((long)strlen(a[0].as.s));
  }
  ov_panic("len expects Array or String");
  return ov_unit();
}

OValue oryn_get(size_t n, OValue *a) {
  if (n != 2 || a[0].tag != OV_ARRAY || a[1].tag != OV_INT) {
    ov_panic("get expects Array and Int");
  }
  long i = a[1].as.i;
  return i >= 0 && (size_t)i < a[0].as.a->len ? ov_some(a[0].as.a->data[i]) : ov_none();
}

OValue ov_method_len(OValue x, size_t n, OValue *a) {
  (void)a;
  if (n != 0) {
    ov_panic("len expects no arguments");
  }
  return oryn_len(1, &x);
}

OValue ov_method_is_empty(OValue x, size_t n, OValue *a) {
  (void)a;
  if (n != 0) {
    ov_panic("is_empty expects no arguments");
  }
  return ov_eq(oryn_len(1, &x), ov_int(0));
}

OValue ov_method_get(OValue x, size_t n, OValue *a) {
  if (n != 1) {
    ov_panic("get expects one index");
  }
  OValue z[2] = {x, a[0]};
  return oryn_get(2, z);
}

OValue ov_method_first(OValue x, size_t n, OValue *a) {
  (void)a;
  if (n != 0) {
    ov_panic("first expects no arguments");
  }
  OValue i = ov_int(0), z[2] = {x, i};
  return oryn_get(2, z);
}

OValue ov_method_last(OValue x, size_t n, OValue *a) {
  (void)a;
  if (n != 0) {
    ov_panic("last expects no arguments");
  }
  if (x.tag != OV_ARRAY) {
    ov_panic("last expects an Array receiver");
  }
  if (!x.as.a->len) {
    return ov_none();
  }
  return ov_some(x.as.a->data[x.as.a->len - 1]);
}

OValue ov_method_contains(OValue x, size_t n, OValue *a) {
  if (n != 1) {
    ov_panic("contains expects one argument");
  }
  if (x.tag == OV_STRING && a[0].tag == OV_STRING) {
    return ov_bool(strstr(x.as.s, a[0].as.s) != NULL);
  }
  if (x.tag == OV_ARRAY) {
    for (size_t i = 0; i < x.as.a->len; i++) {
      if (ov_equal(x.as.a->data[i], a[0])) {
        return ov_bool(1);
      }
    }
    return ov_bool(0);
  }
  ov_panic("contains expects String or Array");
  return ov_unit();
}

OValue ov_method_starts_with(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_STRING || n != 1 || a[0].tag != OV_STRING) {
    ov_panic("starts_with expects strings");
  }
  return ov_bool(strncmp(x.as.s, a[0].as.s, strlen(a[0].as.s)) == 0);
}

OValue ov_method_ends_with(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_STRING || n != 1 || a[0].tag != OV_STRING) {
    ov_panic("ends_with expects strings");
  }
  size_t q = strlen(x.as.s), r = strlen(a[0].as.s);
  return ov_bool(q >= r && !strcmp(x.as.s + q - r, a[0].as.s));
}

static void expect_no_args(const char *name, size_t n) {
  if (n) {
    ov_panic(name);
  }
}

static OValue string_transform(OValue x, int mode) {
  if (x.tag != OV_STRING) {
    ov_panic("expected String");
  }
  const char *start = x.as.s, *end = x.as.s + strlen(x.as.s);
  if (mode == 0) {
    while (start < end && isspace((unsigned char)*start)) {
      start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
      end--;
    }
  }
  char *s = ov_alloc((size_t)(end - start) + 1);
  for (size_t i = 0; i < (size_t)(end - start); i++) {
    s[i] = (char)(mode == 1   ? tolower((unsigned char)start[i])
                  : mode == 2 ? toupper((unsigned char)start[i])
                              : start[i]);
  }
  OValue v = {.tag = OV_STRING, .as.s = s};
  return v;
}

OValue ov_method_trim(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("trim expects no arguments", n);
  return string_transform(x, 0);
}

OValue ov_method_to_lower(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("to_lower expects no arguments", n);
  return string_transform(x, 1);
}

OValue ov_method_to_upper(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("to_upper expects no arguments", n);
  return string_transform(x, 2);
}

OValue ov_method_split(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_STRING || n != 1 || a[0].tag != OV_STRING) {
    ov_panic("split expects a String separator");
  }
  const char *sep = a[0].as.s;
  size_t slen = strlen(sep);
  if (!slen) {
    ov_panic("split separator cannot be empty");
  }
  size_t count = 1;
  for (const char *p = x.as.s; (p = strstr(p, sep)); p += slen) {
    count++;
  }
  OValue *parts = ov_alloc(count * sizeof(*parts));
  const char *p = x.as.s;
  size_t i = 0;
  while (1) {
    const char *q = strstr(p, sep);
    size_t len = q ? (size_t)(q - p) : strlen(p);
    char *s = ov_alloc(len + 1);
    memcpy(s, p, len);
    parts[i++] = (OValue){.tag = OV_STRING, .as.s = s};
    if (!q) {
      break;
    }
    p = q + slen;
  }
  OArray *array = ov_alloc(sizeof(*array));
  array->len = count;
  array->data = parts;
  return (OValue){.tag = OV_ARRAY, .as.a = array};
}

OValue ov_method_concat(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_ARRAY || n != 1 || a[0].tag != OV_ARRAY) {
    ov_panic("concat expects arrays");
  }
  size_t total = x.as.a->len + a[0].as.a->len;
  OValue *r = ov_alloc((total ? total : 1) * sizeof(*r));
  memcpy(r, x.as.a->data, x.as.a->len * sizeof(*r));
  memcpy(r + x.as.a->len, a[0].as.a->data, a[0].as.a->len * sizeof(*r));
  OArray *z = ov_alloc(sizeof(*z));
  z->len = total;
  z->data = r;
  return (OValue){.tag = OV_ARRAY, .as.a = z};
}

OValue ov_method_reverse(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("reverse expects no arguments", n);
  if (x.tag != OV_ARRAY) {
    ov_panic("reverse expects Array");
  }
  OValue *r = ov_alloc((x.as.a->len ? x.as.a->len : 1) * sizeof(*r));
  for (size_t i = 0; i < x.as.a->len; i++) {
    r[i] = x.as.a->data[x.as.a->len - i - 1];
  }
  OArray *z = ov_alloc(sizeof(*z));
  z->len = x.as.a->len;
  z->data = r;
  return (OValue){.tag = OV_ARRAY, .as.a = z};
}

OValue ov_method_slice(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_ARRAY || n != 2 || a[0].tag != OV_INT || a[1].tag != OV_INT) {
    ov_panic("slice expects two Int indexes");
  }
  long from = a[0].as.i, to = a[1].as.i;
  if (from < 0 || to < from || (size_t)to > x.as.a->len) {
    return ov_err(ov_string("invalid slice range"));
  }
  size_t len = (size_t)(to - from);
  OValue *r = ov_alloc((len ? len : 1) * sizeof(*r));
  memcpy(r, x.as.a->data + from, len * sizeof(*r));
  OArray *z = ov_alloc(sizeof(*z));
  z->len = len;
  z->data = r;
  return ov_ok((OValue){.tag = OV_ARRAY, .as.a = z});
}

OValue ov_method_unwrap_or(OValue x, size_t n, OValue *a) {
  if (n != 1 || (x.tag != OV_OPTION && x.tag != OV_RESULT)) {
    ov_panic("unwrap_or expects Option or Result");
  }
  return x.as.sum.kind == 1 ? *x.as.sum.value : a[0];
}

OValue ov_method_is_ok(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("is_ok expects no arguments", n);
  if (x.tag != OV_RESULT) {
    ov_panic("is_ok expects Result");
  }
  return ov_bool(x.as.sum.kind == 1);
}

OValue ov_method_is_err(OValue x, size_t n, OValue *a) {
  (void)a;
  expect_no_args("is_err expects no arguments", n);
  if (x.tag != OV_RESULT) {
    ov_panic("is_err expects Result");
  }
  return ov_bool(x.as.sum.kind == 2);
}

OValue ov_method_map(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_ARRAY || n != 1 || a[0].tag != OV_FUNCTION) {
    ov_panic("map expects a function");
  }
  size_t len = x.as.a->len;
  OValue *items = ov_alloc((len ? len : 1) * sizeof(*items));
  for (size_t i = 0; i < len; i++) {
    items[i] = ov_call(a[0], 1, &x.as.a->data[i]);
  }
  OArray *array = ov_alloc(sizeof(*array));
  array->len = len;
  array->data = items;
  return (OValue){.tag = OV_ARRAY, .as.a = array};
}

OValue ov_method_filter(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_ARRAY || n != 1 || a[0].tag != OV_FUNCTION) {
    ov_panic("filter expects a function");
  }
  OValue *items = ov_alloc((x.as.a->len ? x.as.a->len : 1) * sizeof(*items));
  size_t len = 0;
  for (size_t i = 0; i < x.as.a->len; i++) {
    if (ov_truthy(ov_call(a[0], 1, &x.as.a->data[i]))) {
      items[len++] = x.as.a->data[i];
    }
  }
  OArray *array = ov_alloc(sizeof(*array));
  array->len = len;
  array->data = items;
  return (OValue){.tag = OV_ARRAY, .as.a = array};
}

OValue ov_method_fold(OValue x, size_t n, OValue *a) {
  if (x.tag != OV_ARRAY || n != 2 || a[1].tag != OV_FUNCTION) {
    ov_panic("fold expects an initial value and function");
  }
  OValue result = a[0];
  for (size_t i = 0; i < x.as.a->len; i++) {
    OValue args[2] = {result, x.as.a->data[i]};
    result = ov_call(a[1], 2, args);
  }
  return result;
}

OValue oryn_parse_int(size_t n, OValue *a) {
  if (n != 1 || a[0].tag != OV_STRING) {
    ov_panic("parse_int expects String");
  }
  errno = 0;
  char *end;
  long value = strtol(a[0].as.s, &end, 10);
  return errno || *end ? ov_err(ov_string("invalid Int")) : ov_ok(ov_int(value));
}

OValue oryn_parse_float(size_t n, OValue *a) {
  if (n != 1 || a[0].tag != OV_STRING) {
    ov_panic("parse_float expects String");
  }
  errno = 0;
  char *end;
  double value = strtod(a[0].as.s, &end);
  return errno || *end ? ov_err(ov_string("invalid Float")) : ov_ok(ov_float(value));
}

OValue oryn_to_string(size_t n, OValue *a) {
  if (n != 1) {
    ov_panic("to_string expects one value");
  }
  char b[128];
  switch (a[0].tag) {
  case OV_STRING:
    return a[0];
  case OV_INT:
    snprintf(b, sizeof b, "%ld", a[0].as.i);
    break;
  case OV_FLOAT:
    snprintf(b, sizeof b, "%g", a[0].as.f);
    break;
  case OV_BOOL:
    snprintf(b, sizeof b, "%s", a[0].as.b ? "true" : "false");
    break;
  default:
    ov_panic("value cannot be converted to String");
  }
  return ov_string(b);
}
