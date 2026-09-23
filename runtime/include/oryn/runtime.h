#ifndef ORYN_RUNTIME_H
#define ORYN_RUNTIME_H

#include <stddef.h>

typedef enum {
  OV_UNIT,
  OV_INT,
  OV_FLOAT,
  OV_BOOL,
  OV_STRING,
  OV_ARRAY,
  OV_OPTION,
  OV_RESULT,
  OV_FUNCTION
} OTag;

typedef struct OValue OValue;
typedef OValue (*OFunctionBody)(size_t, OValue *, OValue *);

typedef struct {
  OFunctionBody body;
  size_t env_len;
  OValue *env;
} OFunction;

typedef struct {
  size_t len;
  OValue *data;
} OArray;

struct OValue {
  OTag tag;

  union {
    long i;
    double f;
    int b;
    char *s;
    OArray *a;
    OFunction *fn;

    struct {
      int kind;
      OValue *value;
    } sum;
  } as;
};

void ov_panic(const char *message);
void ov_gc_init(void);
void ov_gc_push(OValue *root);
void ov_gc_pop(size_t count);
void ov_gc_collect(void);
void ov_gc_shutdown(void);
size_t ov_gc_live_bytes(void);
size_t ov_gc_live_objects(void);
size_t ov_gc_collection_count(void);
OValue ov_unit(void);
OValue ov_int(long value);
OValue ov_float(double value);
OValue ov_bool(int value);
OValue ov_string(const char *value);
OValue ov_array(size_t count, OValue *values);
OValue ov_some(OValue value);
OValue ov_none(void);
OValue ov_ok(OValue value);
OValue ov_err(OValue value);
OValue ov_function(OFunctionBody body, size_t env_len, OValue *env);
OValue ov_call(OValue function, size_t count, OValue *args);
int ov_truthy(OValue value);
int ov_is_ctor(OValue value, const char *constructor);
OValue ov_payload(OValue value);

OValue ov_add(OValue a, OValue b);
OValue ov_sub(OValue a, OValue b);
OValue ov_mul(OValue a, OValue b);
OValue ov_div(OValue a, OValue b);
OValue ov_mod(OValue a, OValue b);
OValue ov_eq(OValue a, OValue b);
OValue ov_ne(OValue a, OValue b);
OValue ov_lt(OValue a, OValue b);
OValue ov_le(OValue a, OValue b);
OValue ov_gt(OValue a, OValue b);
OValue ov_ge(OValue a, OValue b);
OValue ov_and(OValue a, OValue b);
OValue ov_or(OValue a, OValue b);
OValue ov_neg(OValue value);
OValue ov_not(OValue value);

OValue oryn_print(size_t count, OValue *args);
OValue oryn_println(size_t count, OValue *args);
OValue oryn_assert(size_t count, OValue *args);
OValue oryn_panic(size_t count, OValue *args);
OValue oryn_len(size_t count, OValue *args);
OValue oryn_get(size_t count, OValue *args);
OValue oryn_parse_int(size_t count, OValue *args);
OValue oryn_parse_float(size_t count, OValue *args);
OValue oryn_to_string(size_t count, OValue *args);

OValue ov_method_len(OValue value, size_t count, OValue *args);
OValue ov_method_is_empty(OValue value, size_t count, OValue *args);
OValue ov_method_get(OValue value, size_t count, OValue *args);
OValue ov_method_first(OValue value, size_t count, OValue *args);
OValue ov_method_last(OValue value, size_t count, OValue *args);
OValue ov_method_contains(OValue value, size_t count, OValue *args);
OValue ov_method_starts_with(OValue value, size_t count, OValue *args);
OValue ov_method_ends_with(OValue value, size_t count, OValue *args);
OValue ov_method_trim(OValue value, size_t count, OValue *args);
OValue ov_method_to_lower(OValue value, size_t count, OValue *args);
OValue ov_method_to_upper(OValue value, size_t count, OValue *args);
OValue ov_method_split(OValue value, size_t count, OValue *args);
OValue ov_method_concat(OValue value, size_t count, OValue *args);
OValue ov_method_reverse(OValue value, size_t count, OValue *args);
OValue ov_method_slice(OValue value, size_t count, OValue *args);
OValue ov_method_unwrap_or(OValue value, size_t count, OValue *args);
OValue ov_method_is_ok(OValue value, size_t count, OValue *args);
OValue ov_method_is_err(OValue value, size_t count, OValue *args);
OValue ov_method_map(OValue value, size_t count, OValue *args);
OValue ov_method_filter(OValue value, size_t count, OValue *args);
OValue ov_method_fold(OValue value, size_t count, OValue *args);

#endif
