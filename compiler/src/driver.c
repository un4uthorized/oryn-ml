#include "oryn/compiler/base.h"
#include "oryn/compiler/codegen.h"
#include "oryn/compiler/parser.h"
#include "oryn/compiler/sema.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ORYN_VERSION "0.1.0"

static void help(FILE *out) {
  fprintf(out,
          "Oryn-ML %s\n"
          "usage:\n"
          "  oryn <input.oryn> [-o output.c]   Emit C (compatible form)\n"
          "  oryn check <input.oryn>           Parse and analyze\n"
          "  oryn build <input.oryn> [-o exe]  Build a native executable\n"
          "  oryn run <input.oryn>              Build and run\n"
          "  oryn --help | --version\n",
          ORYN_VERSION);
}

static Stmts load_program(const char *input, char **source_out) {
  char *source = read_file(input);
  Parser parser = {.src = source, .path = input, .line = 1, .col = 1};
  Stmts program = parse_program(&parser);
  analyze_program(&program, input, source);
  *source_out = source;
  return program;
}

static void emit_file(Stmts *program, const char *input, const char *path) {
  FILE *out = !path ? stdout : fopen(path, "wb");
  if (!out) {
    die("cannot create %s: %s", path, strerror(errno));
  }
  generate_c(out, program, input);
  if (path && fclose(out)) {
    die("cannot finish writing %s", path);
  }
}

static int invoke(const char *c_path, const char *executable) {
  pid_t child = fork();
  if (child < 0) {
    die("cannot start C compiler: %s", strerror(errno));
  }
  if (!child) {
    execlp("cc",
           "cc",
           "-std=gnu11",
           "-O2",
           "-Iruntime/include",
           c_path,
           "runtime/src/runtime.c",
           "-o",
           executable,
           (char *)NULL);
    fprintf(stderr, "oryn: cannot execute cc: %s\n", strerror(errno));
    _exit(127);
  }
  int status;
  if (waitpid(child, &status, 0) < 0) {
    die("cannot wait for C compiler");
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static char *default_output(const char *input) {
  const char *base = strrchr(input, '/');
  base = base ? base + 1 : input;
  size_t n = strlen(base);
  if (n > 5 && !strcmp(base + n - 5, ".oryn")) {
    n -= 5;
  }
  return xstrndup(base, n);
}

int main(int argc, char **argv) {
  if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
    help(stdout);
    return 0;
  }
  if (argc == 2 && !strcmp(argv[1], "--version")) {
    puts("oryn " ORYN_VERSION);
    return 0;
  }
  if (argc < 2) {
    help(stderr);
    return 2;
  }

  const char *command = "emit", *input = argv[1], *output = NULL;
  int arg = 2;
  if (!strcmp(argv[1], "check") || !strcmp(argv[1], "build") || !strcmp(argv[1], "run")) {
    command = argv[1];
    if (argc < 3) {
      help(stderr);
      return 2;
    }
    input = argv[2];
    arg = 3;
  }
  if (arg < argc) {
    if (arg + 2 != argc || strcmp(argv[arg], "-o") || !strcmp(command, "run") ||
        !strcmp(command, "check")) {
      help(stderr);
      return 2;
    }
    output = argv[arg + 1];
  }

  char *source;
  Stmts program = load_program(input, &source);
  (void)source;
  if (!strcmp(command, "check")) {
    return 0;
  }
  if (!strcmp(command, "emit")) {
    emit_file(&program, input, output);
    return 0;
  }

  char c_template[] = "/tmp/oryn-XXXXXX.c";
  int fd = mkstemps(c_template, 2);
  if (fd < 0) {
    die("cannot create temporary C file: %s", strerror(errno));
  }
  FILE *c_file = fdopen(fd, "wb");
  if (!c_file) {
    die("cannot open temporary C file");
  }
  generate_c(c_file, &program, input);
  fclose(c_file);

  char *owned_output = NULL;
  char exe_template[] = "/tmp/oryn-run-XXXXXX";
  if (!strcmp(command, "run")) {
    int exe_fd = mkstemp(exe_template);
    if (exe_fd < 0) {
      die("cannot reserve temporary executable");
    }
    close(exe_fd);
    output = exe_template;
  } else if (!output) {
    owned_output = default_output(input);
    output = owned_output;
  }
  int result = invoke(c_template, output);
  unlink(c_template);
  if (result) {
    return result;
  }
  if (!strcmp(command, "run")) {
    pid_t child = fork();
    if (child < 0) {
      die("cannot run program");
    }
    if (!child) {
      execl(output, output, (char *)NULL);
      _exit(127);
    }
    int status;
    waitpid(child, &status, 0);
    unlink(output);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  }
  free(owned_output);
  return 0;
}
