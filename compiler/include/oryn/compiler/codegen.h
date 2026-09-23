#ifndef ORYN_COMPILER_CODEGEN_H
#define ORYN_COMPILER_CODEGEN_H

#include "oryn/compiler/ast.h"

#include <stdio.h>

/* C is one emission target; future targets can expose independent interfaces. */
void generate_c(FILE *output, Stmts *program, const char *source_path);

#endif
