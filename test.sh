#!/usr/bin/env bash
# Correctness test suite.
set -e
cd "$(dirname "$0")"

echo "[1/6] Dense LU unit test..."
gcc -O2 -march=native -I src tools/unit_test.c src/lu.c src/kernels.c -o /tmp/unit_test -lm
/tmp/unit_test

echo "[1.5/6] Sparse LU unit test..."
gcc -O2 -march=native -I src tools/splu_test.c src/splu.c src/lu.c src/kernels.c -o /tmp/splu_test -lm
/tmp/splu_test

echo "[2/6] Example problems (objective values)..."
echo -n "  diet (expect 1.32):       "; ./lpsolve examples/diet.lp      | grep objective
echo -n "  prodplan (expect 26):     "; ./lpsolve examples/prodplan.lp  | grep objective
echo -n "  transport (expect 94.5):  "; ./lpsolve examples/transport.lp | grep objective

echo "[3/6] Canonical sweep vs GLPK (bounded, well-conditioned)..."
python3 tools/sweep.py | tail -1

echo "[4/6] Differential test vs GLPK (incl. infeasible/unbounded)..."
python3 tools/difftest.py 120 0.4 2>&1 | head -1

echo "[5/6] QP solver vs scipy (analytic + randomized)..."
gcc -O2 -march=native -I src tools/qp_test.c src/qp.c src/lu.c src/kernels.c -o /tmp/qp_test -lm
/tmp/qp_test
gcc -O2 -march=native -I src tools/qpsolve.c src/qp.c src/lu.c src/kernels.c -o /tmp/qpsolve -lm
ok=0; fail=0
for s in $(seq 1 40); do
  if python3 tools/qp_gen.py $s 2>/dev/null | grep -q '^OK'; then ok=$((ok+1)); else fail=$((fail+1)); fi
done
echo "  QP vs scipy: OK=$ok FAIL=$fail"

echo "[6/6] Incremental solving (warm starts vs fresh solves)..."
gcc -O2 -march=native -I src tools/incr_test.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_test -lm
/tmp/incr_test
gcc -O2 -march=native -I src tools/incr_rand.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_rand -lm
/tmp/incr_rand | tail -1

echo "[7/7] Fuzz malformed inputs under ASan/UBSan..."
if command -v gcc >/dev/null; then
  python3 tools/fuzz_inputs.py --iters 80 --seed 7
else
  echo "  (skipped: gcc not available)"
fi

echo "Done."
