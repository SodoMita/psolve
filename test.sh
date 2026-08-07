#!/usr/bin/env bash
# Correctness test suite.
set -e
cd "$(dirname "$0")"

echo "[0/7] PGS boxed-QP (real-time physics kernel) unit test..."
gcc -O2 -march=native -I src tools/pgs_test.c src/pgs.c -o /tmp/pgs_test -lm
/tmp/pgs_test

echo "[1/7] Dense LU unit test..."
gcc -O2 -march=native -I src tools/unit_test.c src/lu.c src/kernels.c -o /tmp/unit_test -lm
/tmp/unit_test

echo "[1.5/7] Sparse LU unit test..."
gcc -O2 -march=native -I src tools/splu_test.c src/splu.c src/lu.c src/kernels.c -o /tmp/splu_test -lm
/tmp/splu_test

echo "[2/7] Example problems (objective values)..."
echo -n "  diet (expect 1.32):       "; ./lpsolve examples/diet.lp      | grep objective
echo -n "  prodplan (expect 26):     "; ./lpsolve examples/prodplan.lp  | grep objective
echo -n "  transport (expect 94.5):  "; ./lpsolve examples/transport.lp | grep objective

echo "[3/7] Canonical sweep vs GLPK (bounded, well-conditioned)..."
python3 tools/sweep.py | tail -1

echo "[4/7] Differential test vs GLPK (incl. infeasible/unbounded)..."
python3 tools/difftest.py 120 0.4 2>&1 | head -1

echo "[5/7] QP solver vs scipy (analytic + randomized)..."
gcc -O2 -march=native -I src tools/qp_test.c src/qp.c src/err.c src/lu.c src/kernels.c -o /tmp/qp_test -lm
/tmp/qp_test
gcc -O2 -march=native -I src tools/qpsolve.c src/qp.c src/err.c src/lu.c src/kernels.c -o /tmp/qpsolve -lm
ok=0; fail=0
for s in $(seq 1 40); do
  if python3 tools/qp_gen.py $s 2>/dev/null | grep -q '^OK'; then ok=$((ok+1)); else fail=$((fail+1)); fi
done
echo "  QP vs scipy: OK=$ok FAIL=$fail"

echo "[5.5/7] MIP solver (branch-and-bound) vs brute force..."
gcc -O2 -march=native -I src tools/mip_test.c src/mip.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/mip_test -lm
/tmp/mip_test
if [ -f /tmp/mip_verify.py ]; then
  python3 tools/mip_verify.py 0 | tail -1
fi

echo "[6/7] Incremental solving (warm starts vs fresh solves)..."
gcc -O2 -march=native -I src tools/incr_test.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_test -lm
/tmp/incr_test
gcc -O2 -march=native -I src tools/incr_rand.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_rand -lm
/tmp/incr_rand | tail -1

echo "[7/7] Fuzz malformed inputs under ASan/UBSan..."
if command -v gcc >/dev/null; then
  python3 tools/fuzz_inputs.py --iters 80 --seed 7
else
  echo "  (skipped: gcc not available)"
fi

echo "Done."
