#ifndef ORYN_COMPILER_SEMA_H
#define ORYN_COMPILER_SEMA_H

#include "oryn/compiler/ast.h"

void analyze_program(Stmts *program, const char *path, const char *source);

#endif
