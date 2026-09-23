#ifndef ORYN_COMPILER_BASE_H
#define ORYN_COMPILER_BASE_H

#include <stddef.h>

/* Process-wide diagnostics and allocation helpers. */
void die(const char *fmt, ...);
void *xmalloc(size_t size);
char *xstrndup(const char *text, size_t length);
char *xstrdup(const char *text);
char *read_file(const char *path);

#endif
