#!/usr/bin/env bash
#
# Build the Mocha 1995 engine to a WebAssembly ES module for the playground.
#
# Requires Emscripten (`emcc` on PATH).  If you used emsdk, run:
#     source /path/to/emsdk/emsdk_env.sh
#
# Output: web/public/engine/mocha.mjs + mocha.wasm
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out/wasm"
DEST="$ROOT/web/public/engine"

ENGINE_SRCS=(
  mo_array mo_atom mo_bcode mo_bool mo_cntxt mo_date mo_emit mo_fun
  mo_math mo_num mo_obj mo_parse mo_scan mo_scope mo_str mocha mochaapi
  mochalib prmjtime prtime prarena prhash prprf prdtoa log2 longlong
)

if ! command -v emcc >/dev/null 2>&1; then
  echo "error: emcc not found. Install Emscripten and 'source emsdk_env.sh'." >&2
  exit 1
fi

mkdir -p "$OUT" "$DEST"

echo "compiling engine objects..."
for f in "${ENGINE_SRCS[@]}"; do
  emcc -w -O2 -I"$ROOT/include" -c "$ROOT/src/$f.c" -o "$OUT/$f.o"
done

echo "linking mocha.mjs..."
emcc -O2 -I"$ROOT/include" "$ROOT/src/mo_web.c" "$OUT"/*.o \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web,node \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=33554432 \
  -s "EXPORTED_FUNCTIONS=['_mocha_run_json','_free','_malloc']" \
  -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']" \
  -o "$OUT/mocha.mjs"

cp "$OUT/mocha.mjs" "$OUT/mocha.wasm" "$DEST/"
echo "done -> $DEST/mocha.mjs (+ mocha.wasm)"
