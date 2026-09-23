CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
GENERATED_CFLAGS ?= -std=gnu11 -O2 -Wno-unused-function -Wno-unused-variable
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || xcrun --find clang-format 2>/dev/null)
COMPILER_SOURCES := \
	compiler/src/driver.c \
	compiler/src/base.c \
	compiler/src/ast.c \
	compiler/src/lexer.c \
	compiler/src/parser.c \
	compiler/src/sema.c \
	compiler/src/codegen_c.c
COMPILER_HEADERS := $(wildcard compiler/include/oryn/compiler/*.h)
RUNTIME_SOURCE := runtime/src/runtime.c
RUNTIME_HEADER := runtime/include/oryn/runtime.h
C_FORMAT_FILES := $(COMPILER_SOURCES) $(COMPILER_HEADERS) $(RUNTIME_SOURCE) $(RUNTIME_HEADER)

.PHONY: all test sanitize examples format format-check lint clean

all: bin/oryn

bin/oryn: $(COMPILER_SOURCES) $(COMPILER_HEADERS) | bin
	$(CC) $(CFLAGS) -Icompiler/include -o $@ $(COMPILER_SOURCES)

bin:
	mkdir -p bin

examples: all
	@for f in examples/*.oryn; do \
		name=$$(basename "$$f" .oryn); \
		bin/oryn "$$f" -o "bin/$$name.c" && \
		$(CC) $(GENERATED_CFLAGS) -Iruntime/include "bin/$$name.c" $(RUNTIME_SOURCE) -o "bin/$$name" || exit 1; \
	done

test: all
	sh tests/run.sh

sanitize: all
	sh tests/sanitize.sh

format:
	@test -n "$(CLANG_FORMAT)" || (echo "clang-format not found" >&2; exit 1)
	$(CLANG_FORMAT) -i $(C_FORMAT_FILES)

format-check:
	@test -n "$(CLANG_FORMAT)" || (echo "clang-format not found" >&2; exit 1)
	$(CLANG_FORMAT) --dry-run --Werror $(C_FORMAT_FILES)

lint: format-check
	$(CC) $(CFLAGS) -Icompiler/include -fsyntax-only $(COMPILER_SOURCES)
	$(CC) $(CFLAGS) -Iruntime/include -fsyntax-only $(RUNTIME_SOURCE)

clean:
	rm -rf bin
