#!/usr/bin/env bash
# Correctness test suite.
set -e
cd "$(dirname "$0")"

echo "[1/5] Dense LU unit test..."
gcc -O2 -march=native -I src tools/unit_test.c src/lu.c src/kernels.c -o /tmp/unit_test -lm
/tmp/unit_test

echo "[1.5/5] Sparse LU unit test..."
gcc -O2 -march=native -I src tools/splu_test.c src/splu.c src/lu.c src/kernels.c -o /tmp/splu_test -lm
/tmp/splu_test

echo "[2/5] Example problems (objective values)..."
echo -n "  diet (expect 1.32):       "; ./lpsolve examples/diet.lp      | grep objective
echo -n "  prodplan (expect 26):     "; ./lpsolve examples/prodplan.lp  | grep objective
echo -n "  transport (expect 94.5):  "; ./lpsolve examples/transport.lp | grep objective

echo "[3/5] Canonical sweep vs GLPK (bounded, well-conditioned)..."
python3 tools/sweep.py | tail -1

echo "[4/5] Differential test vs GLPK (incl. infeasible/unbounded)..."
python3 tools/difftest.py 150 0.4 2>&1 | head -1

echo "Done."
