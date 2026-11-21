#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-clang}"
WASM_LD="${WASM_LD:-wasm-ld}"
OUT=wasm/vexaph.wasm
OBJ=build/obj

command -v "$CC" >/dev/null || { echo "clang not found (need clang with wasm32 target)"; exit 1; }
command -v "$WASM_LD" >/dev/null || WASM_LD="wasm-ld-18"
command -v "$WASM_LD" >/dev/null || { echo "wasm-ld not found"; exit 1; }

CFLAGS="--target=wasm32 -O3 -std=gnu11 -nostdlibinc -isystem libc/include -Iengine/include \
  -mbulk-memory -msimd128 -mnontrapping-fptoint -fno-math-errno -fvisibility=hidden -Wall -Wextra"

SRCS="$(ls engine/src/core/*.c engine/src/brain/*.c engine/src/book/*.c engine/src/platform/platform.c engine/src/interface/uci.c engine/src/interface/wasm_api.c) libc/vx_libc.c"

mkdir -p "$OBJ" wasm
OBJS=""
for f in $SRCS; do
  o="$OBJ/$(echo "$f" | tr '/' '_').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find engine/include libc/include -newer "$o" -name '*.h' -print -quit)" ]; then
    $CC $CFLAGS -c "$f" -o "$o"
  fi
  OBJS="$OBJS $o"
done

$WASM_LD $OBJS -o "$OUT" \
  --no-entry --export-dynamic --export=malloc --export=free --export=__heap_base \
  --stack-first -z stack-size=8388608 \
  --initial-memory=67108864 --max-memory=2147483648 \
  --gc-sections --strip-all

echo "built $OUT ($(wc -c < "$OUT") bytes)"
