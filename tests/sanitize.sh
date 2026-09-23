#!/bin/sh
set -eu

CC=${CC:-cc}
mkdir -p bin/sanitize

for source in examples/*.oryn; do
  name=$(basename "$source" .oryn)
  generated="bin/sanitize/$name.c"
  executable="bin/sanitize/$name"
  bin/oryn "$source" -o "$generated"
  "$CC" -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iruntime/include "$generated" runtime/src/runtime.c -o "$executable"
  "$executable" >/dev/null
done

echo "sanitizers passed for all example programs"
