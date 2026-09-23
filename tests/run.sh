#!/bin/sh
set -eu

CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=gnu11 -O2 -Wno-unused-function -Wno-unused-variable}
mkdir -p bin/tests

count=0
for source in examples/*.oryn; do
  name=$(basename "$source" .oryn)
  generated="bin/tests/$name.c"
  executable="bin/tests/$name"
  bin/oryn "$source" -o "$generated"
  $CC $CFLAGS -Iruntime/include "$generated" runtime/src/runtime.c -o "$executable"
  "$executable" >"bin/tests/$name.out"
  diff -u "tests/expected/$name.out" "bin/tests/$name.out"
  count=$((count + 1))
done

for invalid in tests/invalid.oryn tests/invalid/*.oryn; do
  if bin/oryn check "$invalid" 2>/dev/null; then
    echo "expected $invalid to fail" >&2
    exit 1
  fi
done

bin/oryn check examples/v01.oryn
bin/oryn run examples/hello.oryn >bin/tests/cli-run.out
diff -u tests/expected/hello.out bin/tests/cli-run.out

$CC $CFLAGS -Iruntime/include tests/runtime_gc.c runtime/src/runtime.c -o bin/tests/runtime_gc
bin/tests/runtime_gc

echo "$count example programs compiled and passed"
