#!/usr/bin/env bash
# Correctness test suite.
set -e
cd "$(dirname "$0")"

echo "[0/7] PGS boxed-QP (real-time physics kernel) unit test..."
gcc -O2 -march=native -I src tools/pgs_test.c src/pgs.c -o /tmp/pgs_test -lm
/tmp/pgs_test

echo "[0.5/7] Fixed-point PGS (integer) unit test + determinism..."
gcc -O2 -march=native -I src tools/pgs_fixed_test.c src/pgs_fixed.c src/pgs.c -o /tmp/pgs_fixed_test -lm
/tmp/pgs_fixed_test

echo "[1/7] Dense LU unit test..."
gcc -O2 -march=native -I src tools/unit_test.c src/lu.c src/kernels.c -o /tmp/unit_test -lm
/tmp/unit_test

echo "[1.5/7] Sparse LU unit test..."
gcc -O2 -march=native -I src tools/splu_test.c src/splu.c src/lu.c src/kernels.c src/err.c -o /tmp/splu_test -lm
/tmp/splu_test

echo "[2/7] Example problems (objective values)..."
echo -n "  diet (expect 1.32):       "; ./lpsolve examples/diet.lp      | grep objective
echo -n "  prodplan (expect 26):     "; ./lpsolve examples/prodplan.lp  | grep objective
echo -n "  transport (expect 94.5):  "; ./lpsolve examples/transport.lp | grep objective
echo -n "  free variable (expect 3): "; ./lpsolve examples/free_vars.lp | grep objective

if command -v glpsol >/dev/null 2>&1; then
  echo "[3/7] Canonical sweep vs GLPK (bounded, well-conditioned)..."
  python3 tools/sweep.py | tail -1

  echo "[4/7] Differential test vs GLPK (incl. infeasible/unbounded)..."
  python3 tools/difftest.py 120 0.4 2>&1 | head -1
else
  echo "[3-4/7] GLPK differential tests SKIPPED (glpsol not installed)"
fi

echo "[5/7] QP solver vs scipy (analytic + randomized)..."
gcc -O2 -march=native -I src tools/qp_test.c src/qp.c src/err.c src/lu.c src/kernels.c -o /tmp/qp_test -lm
/tmp/qp_test
gcc -O2 -march=native -I src tools/qpsolve.c src/qp.c src/err.c src/lu.c src/kernels.c -o /tmp/qpsolve -lm
ok=0; fail=0
for s in $(seq 1 40); do
  if python3 tools/qp_gen.py $s 2>/dev/null | grep -q '^OK'; then ok=$((ok+1)); else fail=$((fail+1)); fi
done
echo "  QP vs scipy: OK=$ok FAIL=$fail"
# qp_gen only builds strictly positive-definite Q.  qp_diff also covers
# singular PSD Q, Q=0, m=0 and duplicated rows, and certifies the answer with
# the KKT conditions (necessary AND sufficient for a convex QP) plus an exact
# recession-direction test, so it catches infeasible/unbounded/non-stationary
# points that an objective comparison against SLSQP cannot.
python3 tools/qp_diff.py 200 4242 | head -2

echo "[5.5/7] MIP solver (branch-and-bound) vs brute force..."
gcc -O2 -march=native -I src tools/mip_test.c src/mip.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/mip_test -lm
/tmp/mip_test
# NOTE: this used to read `if [ -f /tmp/mip_verify.py ]`, a path that never
# exists, so the MIP verification silently never ran.  mip_diff.py replaces it
# and additionally checks statuses and the returned point, not just the
# objective of runs that happened to come back OPTIMAL.
python3 tools/mip_diff.py 400 12345 | head -2
python3 tools/mip_verify.py 0 | tail -1

echo "[5.75/7] Fully free LP/MIP variables + incremental API..."
gcc -O2 -march=native -I src tools/free_var_test.c src/mip.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c -o /tmp/free_var_test -lm
/tmp/free_var_test

echo "[6/7] Incremental solving (warm starts vs fresh solves)..."
gcc -O2 -march=native -I src tools/incr_test.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_test -lm
/tmp/incr_test
gcc -O2 -march=native -I src tools/incr_rand.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/incr_rand -lm
/tmp/incr_rand | tail -1

echo "[7/7] Fuzz malformed inputs under ASan/UBSan..."
if command -v gcc >/dev/null; then
  python3 tools/fuzz_inputs.py --iters 80 --seed 7
  python3 tools/fuzz_fzn.py --iters 120 --seed 7
else
  echo "  (skipped: gcc not available)"
fi

echo "[8/8] FlatZinc reader + solver bridge (Phase 3, linear subset)..."
make fznsolve >/dev/null 2>&1
./fznsolve examples/fzn/satisfy_lin.fzn | grep -E "x1 =|x2 =" | tr '\n' ' '
echo "(expect x1=6 x2=4)"
./fznsolve examples/fzn/min_lin.fzn | grep -E "x1 =|x2 =" | tr '\n' ' '
echo "(expect x1=1 x2=1)"
./fznsolve examples/fzn/max_lin.fzn | grep -E "x1 =|x2 =" | tr '\n' ' '
echo "(expect x1=0 x2=12)"
./fznsolve examples/fzn/bool_logic.fzn | grep -E "a =|b =|andr|orr" | tr '\n' ' '
echo "(expect a=0 b=1 andr=0 orr=1)"
./fznsolve examples/fzn/mip_max.fzn | grep -E "x1 =|x2 =" | tr '\n' ' '
echo "(expect x1=4 x2=0, MIP integral)"
./fznsolve examples/fzn/float_lin.fzn | grep 'x = array1d'
echo "(expect float array [1, 1.5])"
./fznsolve examples/fzn/table.fzn | grep 'x = array1d'
echo "(expect table tuple [2, 3])"
./fznsolve examples/fzn/circuit.fzn | grep 's = array1d'
echo "(expect a Hamiltonian successor cycle)"
./fznsolve examples/fzn/table_sat.fzn | grep -E "x1 =|x2 =" | tr '\n' ' '
echo "(expect a table row, e.g. x1=1 x2=2)"
./fznsolve examples/fzn/table_opt.fzn | grep -E "x1 =|x2 =|x3 =|obj =" | tr '\n' ' '
echo "(expect row (6,5,3) with obj=14)"
if ./fznsolve examples/fzn/table_unsat.fzn | grep -q "=====UNSATISFIABLE====="; then
  echo "table_unsat: UNSATISFIABLE (expect UNSATISFIABLE)  OK"
else
  echo "table_unsat: FAIL (expected UNSATISFIABLE)"
fi
echo "  cumulative_sat:"
./fznsolve examples/fzn/cumulative_sat.fzn | grep -E "s1 =|s2 =" | tr '\n' ' '
echo "(expect two non-overlapping 2-timestep tasks on a capacity-1 resource)"
echo "  cumulative_unsat:"
if ./fznsolve examples/fzn/cumulative_unsat.fzn | grep -q "=====UNSATISFIABLE====="; then
  echo "cumulative_unsat: UNSATISFIABLE (expect UNSATISFIABLE)  OK"
else
  echo "cumulative_unsat: FAIL (expected UNSATISFIABLE)"
fi
echo -n "cumulative_verify (randomized, vs brute force): "
python3 tools/cumulative_verify.py 200 777 | sed 's/.*: //'

echo "[8.25/8] FlatZinc strict/reified-int + float + table/circuit semantics..."
python3 tools/fzn_semantics_test.py
echo -n "table_verify (randomized, vs brute force): "
python3 tools/table_verify.py 250 20240607 | sed 's/.*: //'
echo -n "extrema_verify (randomized, vs brute force): "
python3 tools/extrema_verify.py 300 4242 | sed 's/.*: //'

if command -v minizinc >/dev/null 2>&1; then
  echo "[8.5/8] MiniZinc differential (compile .mzn -> fzn -> psolve vs Gecode)..."
  python3 tools/mzn_diff.py | tail -1
else
  echo "[8.5/8] MiniZinc differential SKIPPED (minizinc not installed)"
fi

echo "[9/10] Fixed-point exact-rational LP solver (fxsolve) vs double lpsolve..."
make fxsolve >/dev/null 2>&1
./fxsolve examples/prodplan.lp | grep -E "objective \(dec\):" | tr '\n' ' '; echo "(expect 26)"
./fxsolve examples/diet.lp | grep -E "objective \(dec\):" | tr '\n' ' '; echo "(expect 1.32 exact)"
./fxsolve examples/transport.lp | grep -E "objective \(dec\):" | tr '\n' ' '; echo "(expect 94.5 exact)"
echo -n "  exact demo (double vs fixed): "
echo -n "double="; ./lpsolve examples/exact.lp | grep -oE "objective:.*"
echo -n "  fixed="; ./fxsolve examples/exact.lp | grep -oE "objective \(exact\):.*"
echo -n "fx_exact_test (exact feasibility/objective in Fractions, 64-vs-128-bit): "
python3 tools/fx_exact_test.py 200 7 | head -1 | sed 's/.*: //'
echo -n "fx_verify (random feasible+arbitrary LPs vs double, incl. status): "
python3 tools/fx_verify.py 300 99 | sed 's/.*: //'
echo -n "fx_bench (examples): "
make fx_bench >/dev/null 2>&1
./fx_bench examples/prodplan.lp examples/diet.lp examples/transport.lp | tail -1

echo "[10/10] Out-of-memory injection (every allocation made to fail in turn)..."
python3 tools/oom_test.py | tail -3

echo "Done."
