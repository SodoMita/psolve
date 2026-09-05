#!/bin/sh
# wasm_build.sh -- build psolve's LP+QP cores and tools/psolve_web.c into one
# WebAssembly module, exporting only the psw_* bridge.
#
#   tools/wasm_build.sh                     autodetect: emcc, then wasi-sdk
#   tools/wasm_build.sh --target=emscripten  force a toolchain (or =wasi, =native)
#   tools/wasm_build.sh --check               compile + run the host test natively
#   tools/wasm_build.sh --out DIR              output directory (default: build)
#
# Why this lives in psolve: every consumer that hand-rolls its own wasm shim
# re-derives psolve's semantics, and the last one to do it renamed QP status -1
# to "INFEASIBLE" (docs/CURV_PS_PLAN.md P0.1) and shimmed setjmp with abort().
# The bridge plus this script are the psolve-owned answer, so the ABI and the
# vocabulary are defined once, next to the solver.
#
# Flags worth knowing about:
#   -fvisibility=hidden and an export list derived from the object file, so the
#       public surface is tools/psolve_web.h and nothing else -- it cannot drift.
#   -O2 -flto by default: these loops are compute-bound, and the size-first -Oz
#       costs real frame time.  Override with PSOLVE_WASM_CFLAGS.
#   no setjmp/longjmp shim: every psw_* path reports a status code.
set -e
cd "$(dirname "$0")/.."        # repository root

SRC="src/err.c src/qp.c src/lu.c src/splu.c src/solver.c src/kernels.c"
OUT=build TARGET=auto CHECK=0
while [ $# -gt 0 ]; do
    case "$1" in
        --target=*) TARGET="${1#--target=}" ;;
        --target)   shift; TARGET="$1" ;;
        --out=*)    OUT="${1#--out=}" ;;
        --out)      shift; OUT="$1" ;;
        --check)    CHECK=1 ;;
        -h|--help)  sed -n '2,25p' "$0"; exit 0 ;;
        *)          echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$OUT"
CFLAGS_WASM="${PSOLVE_WASM_CFLAGS:--O2 -flto -fvisibility=hidden -fno-stack-protector}"

list_exports()      # the psw_* symbols of a built object/module
{
    nm -g --defined-only "$1" 2>/dev/null | awk '$3 ~ /^psw_/ {print "  " $3}' || true
}

if [ "$CHECK" = 1 ] || [ "$TARGET" = native ]; then
    echo "== native check of the bridge (no wasm toolchain required) =="
    ${CC:-cc} -std=gnu11 -Wall -Wextra -O2 -I src -I tools \
        tools/psw_test.c tools/psolve_web.c $SRC -o "$OUT/psw_test" -lm
    "$OUT/psw_test"
    ${CC:-cc} -std=gnu11 -Wall -Wextra -O2 -fvisibility=hidden -c \
        -I src -I tools tools/psolve_web.c -o "$OUT/psolve_web.o"
    echo "== exported symbols =="
    list_exports "$OUT/psolve_web.o"
    exit 0
fi

if [ "$TARGET" = auto ]; then
    if command -v emcc >/dev/null 2>&1; then TARGET=emscripten
    elif [ -n "$WASI_SDK_PATH" ] && [ -x "$WASI_SDK_PATH/bin/clang" ]; then TARGET=wasi
    elif [ -x /opt/wasi-sdk/bin/clang ]; then WASI_SDK_PATH=/opt/wasi-sdk; TARGET=wasi
    else
        echo "no wasm toolchain found: need emcc, or wasi-sdk in \$WASI_SDK_PATH or /opt/wasi-sdk" >&2
        echo "to verify the bridge without one:  tools/wasm_build.sh --check" >&2
        exit 3
    fi
fi

if [ "$TARGET" = emscripten ]; then
    command -v emcc >/dev/null 2>&1 || { echo "emcc not found (target=emscripten)" >&2; exit 3; }
    rm -rf "$OUT/objs"; mkdir -p "$OUT/objs"
    for f in tools/psolve_web.c $SRC; do
        emcc -std=gnu11 $CFLAGS_WASM -I src -I tools -c "$f" -o "$OUT/objs/$(basename "$f" .c).o"
    done
    EXPS=$(nm -g --defined-only "$OUT/objs/psolve_web.o" | awk '$3 ~ /^psw_/ {printf "\"%s\",", $3}' | sed 's/,$//; s/^/[/; s/$/]/')
    emcc $CFLAGS_WASM "$OUT"/objs/*.o -o "$OUT/psolve.mjs" \
        -sERROR_ON_UNDEFINED_SYMBOLS=0 -sEXPORTED_FUNCTIONS="$EXPS" \
        -sEXPORT_ES6=1 -sENVIRONMENT=browser,worker -sMODULARIZE=1 -sALLOW_MEMORY_GROWTH=1
    rm -rf "$OUT/objs"
    echo "wrote $OUT/psolve.mjs (ES module, MODULARIZE, memory growth enabled)"
    list_exports "$OUT/psolve.mjs.wasm" 2>/dev/null || true
elif [ "$TARGET" = wasi ]; then
    CLANG="${WASI_SDK_PATH:-/opt/wasi-sdk}/bin/clang"
    [ -x "$CLANG" ] || { echo "wasi-sdk clang not found at $CLANG" >&2; exit 3; }
    rm -rf "$OUT/objs"; mkdir -p "$OUT/objs"
    for f in tools/psolve_web.c $SRC; do
        "$CLANG" --target=wasm32-wasi -std=gnu11 $CFLAGS_WASM -I src -I tools \
            -c "$f" -o "$OUT/objs/$(basename "$f" .c).o"
    done
    EXPORTS=""
    for s in $(nm -g --defined-only "$OUT/objs/psolve_web.o" | awk '$3 ~ /^psw_/ {print $3}'); do
        EXPORTS="$EXPORTS -Wl,--export=$s"
    done
    [ -n "$EXPORTS" ] || { echo "no psw_* exports found in the bridge object" >&2; exit 1; }
    "$CLANG" --target=wasm32-wasi $CFLAGS_WASM "$OUT"/objs/*.o $EXPORTS \
        -Wl,--no-entry -Wl,--allow-undefined -Wl,--gc-sections -o "$OUT/psolve.wasm" -lm
    rm -rf "$OUT/objs"
    echo "wrote $OUT/psolve.wasm (wasi; exports listed below, everything else internalised)"
    for s in $(nm -g --defined-only "$OUT/psolve.wasm" 2>/dev/null | awk '$3 ~ /^psw_/ {print "  " $3}'); do printf "%s" "$s "; done; echo
else
    echo "unknown --target: $TARGET (use auto, emscripten, wasi or native)" >&2
    exit 2
fi
