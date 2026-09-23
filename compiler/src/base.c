#include "oryn/compiler/base.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void die(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "oryn: ");
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  va_end(args);
  exit(1);
}

void *xmalloc(size_t n) {
  void *ptr = calloc(1, n);
  if (!ptr) {
    die("out of memory");
  }
  return ptr;
}

char *xstrndup(const char *s, size_t n) {
  char *copy = xmalloc(n + 1);
  memcpy(copy, s, n);
  return copy;
}

char *xstrdup(const char *s) {
  return xstrndup(s, strlen(s));
}

char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    die("cannot open %s: %s", path, strerror(errno));
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    die("cannot seek %s", path);
  }
  long length = ftell(file);
  if (length < 0) {
    die("cannot determine the size of %s", path);
  }
  rewind(file);
  char *source = xmalloc((size_t)length + 1);
  if (fread(source, 1, (size_t)length, file) != (size_t)length) {
    die("cannot read %s", path);
  }
  fclose(file);
  return source;
}
