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
gcc -O2 -march=native -I src tools/api_test.c src/solver.c src/qp.c src/mip.c src/fx.c src/pgs.c src/pgs_fixed.c src/fzn.c src/splu.c src/lu.c src/kernels.c src/err.c src/parser.c -o /tmp/api_test -lm
/tmp/api_test

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
# QP convexity-gate differential (roadmap 6.5): the pre-change gate screened
# only 1x1/2x2 principal minors, so n>=3 symmetric indefinite Q whose
# negativity lives in a larger minor PASSED (diag 1, off-diag -0.9: 2x2
# minors 0.19, eigenvalue -0.8), and the active-set printed the stationary
# origin as an "optimum" on problems unbounded below.  Now a complete
# symmetrized complete-pivoting elimination scan certifies PSD (negative
# pivot, or an off-diagonal tail beyond the scaled tolerance, refuses with
# QP_NON_CONVEX).  Discriminating: on the pre-change binary the tool
# reproduces 103 fabricated status-0 verdicts (25 with objectives wrong vs
# the true box optima); post-change it must see 0, keep 120 asymmetric/tiny-
# perturbation over-refusals paperwork-free, and not over-block scale-mixed
# genuine PSD.  Hard gate: rc matters.
python3 tools/qp_psd_verify.py 120 20260815 || { echo "qp_psd_verify: FAIL"; exit 1; }

echo "[5.2/7] QP cooperative stop + millisecond-precision time limits (Phase 4)..."
gcc -O2 -march=native -I src tools/qp_stop_test.c src/qp.c src/err.c src/lu.c src/kernels.c -o /tmp/qp_stop_test -lm
/tmp/qp_stop_test
# Verifies the CLI -t/--time-limit budget is ITIMER_REAL (microsecond
# resolution), not alarm() (whole-second, rounds up): a 50ms budget must fire
# sub-second.
gcc -O2 -march=native -I tools tools/tlimit_test.c -o /tmp/tlimit_test -lm
/tmp/tlimit_test

echo "[5.3/7] Re-entrant zero-malloc arena (QP/LP/fx, Phase 4)..."
# Links --wrap=malloc/calloc/realloc/free so every libc heap call is counted;
# verifies the solve paths make ZERO libc heap calls while an arena is active.
gcc -O2 -march=native -DARENA_TEST_WRAP -I src tools/arena_test.c src/qp.c \
    src/mip.c src/fx.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c \
    src/parser.c -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
    -Wl,--wrap=free -o /tmp/arena_test -lm
/tmp/arena_test

echo "[5.4/7] Arena over the FlatZinc/CP/MIP bridge (Phase 4 port coverage)..."
# Same zero-libc-heap guarantee, extended to the CP engine, table/cumulative
# encodings and the MIP bridge, plus bit-identical arena-vs-libc results.
gcc -O2 -march=native -DARENA_TEST_WRAP -I src tools/arena_fzn_test.c src/fzn.c \
    src/mip.c src/fx.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c \
    src/parser.c -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
    -Wl,--wrap=free -o /tmp/arena_fzn_test -lm
/tmp/arena_fzn_test
# Missed free-routing is a libc abort in the wrapped build; the sanitizer
# build turns it into a precise ASan diagnostic.
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I src \
    tools/arena_fzn_test.c src/fzn.c src/mip.c src/fx.c src/err.c src/solver.c \
    src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/arena_fzn_asan -lm
/tmp/arena_fzn_asan

echo "[5.45/7] Error protocol: caller-owned per-thread frames (Phase 6.3, AUDIT #2)..."
# The retired process-global jmp_buf/active/code triple refused a second
# thread's handler outright and made any concurrent failure a cross-thread
# longjmp.  Caller-owned PSolveErrFrame storage + thread-local chain state
# closes that; these tests pin both the single-thread semantics (nested
# frames, realloc/calloc guards, no-frame exit, checked pop violation) and
# 8-thread concurrent solves + forced OOM recoveries + per-thread stop
# callbacks.  Discriminating: neither test compiles against the pre-change
# err.h (API absent); an old-API reproducer showed psolve_try() refusing a
# second thread's handler.
gcc -O2 -march=native -Wall -Wextra -I src tools/err_proto_test.c src/err.c -o /tmp/err_proto_test -lm
/tmp/err_proto_test || { echo "err_proto_test: FAIL"; exit 1; }
gcc -O2 -march=native -Wall -Wextra -pthread -I src tools/err_mt_test.c src/err.c \
    src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/err_mt_test -lm
/tmp/err_mt_test || { echo "err_mt_test: FAIL"; exit 1; }
# Same multithreaded test under ThreadSanitizer: must produce no reports.
# (Skipped with a note if this toolchain lacks TSan.)
if gcc -O1 -g -fsanitize=thread -pthread -I src tools/err_mt_test.c src/err.c \
    src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/err_mt_tsan -lm 2>/dev/null; then
    /tmp/err_mt_tsan || { echo "err_mt_tsan: FAIL"; exit 1; }
else
    echo "err_mt_tsan: SKIP (no -fsanitize=thread on this toolchain)"
fi
# Objective archive gate: the whole library now exports ZERO non-TLS mutable
# data symbols (pre-change err.o alone had four: psolve_env/psolve_active/
# psolve_code/psolve_stop_fn); the remaining statics are _Thread_local.
if nm -g --defined-only src/err.o src/solver.o src/mip.o src/fzn.o src/qp.o \
     src/fx.o src/parser.o src/splu.o src/lu.o src/kernels.o 2>/dev/null | \
     awk '$2 ~ /[BCDGS]/ {found=1} END {exit found?0:1}'; then
    echo "global-state gate: FAIL (library exports mutable data symbols)"
    exit 1
else
    echo "global-state gate: no exported mutable data symbols in the library"
fi

echo "[5.5/7] MIP solver (branch-and-bound) vs brute force..."
gcc -O2 -march=native -I src tools/mip_test.c src/mip.c src/fx.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c src/parser.c -o /tmp/mip_test -lm
/tmp/mip_test
# Contradictory variable bounds (l > u => certified INFEASIBLE) and duplicate
# sparse triplets (merged by summation), each vs an independent reference.
echo -n "lp_form_verify (l>u bounds + duplicate triplets, vs scipy): "
python3 tools/lp_form_verify.py 150 20260815 | sed 's/.*: //'
# NOTE: this used to read `if [ -f /tmp/mip_verify.py ]`, a path that never
# exists, so the MIP verification silently never ran.  mip_diff.py replaces it
# and additionally checks statuses and the returned point, not just the
# objective of runs that happened to come back OPTIMAL.
python3 tools/mip_diff.py 400 12345 | head -2
# Farkas fast path: Phase-I dual ray re-verified with directed rounding; must
# certify the pinned cycle relaxations with zero exact re-solves and keep
# brute-force verdict parity everywhere (discriminating: fails on a
# pre-change binary because it has no fast path).  Hard gate: rc matters.
python3 tools/farkas_verify.py 150 20260815 || { echo "farkas_verify: FAIL"; exit 1; }
# Exact-or-UNKNOWN promotion for scale-mixed LPs (AUDIT not-done #4, roadmap
# 6.1): exactly-feasible instances with ~1e-13 coefficients against ~1e25
# bounds must never be reported INFEASIBLE (the pre-change fabrication of
# record); exactly-infeasible ones keep their verdict only when the directed-
# rounding box certificate proves it, else honest NUMERICAL_FAILURE/UNKNOWN;
# well-scaled data must be untouched (scipy verdict parity).  Discriminating:
# on the pre-change binary it reproduces 6 fabricated INFEASIBLE verdicts.
# Hard gate: rc matters.
python3 tools/lp_scale_verify.py 60 20260815 || { echo "lp_scale_verify: FAIL"; exit 1; }
python3 tools/mip_verify.py 0 | tail -1
# Adversarial differential test for the sound FBBT bound tightening: mixes
# tiny coefficients (1e-13) with large variable magnitudes and all three
# relation types, checking status + objective + returned point vs brute force,
# plus the audit counterexample (1e-13*x + y <= 1 with x=-1e13 must yield y=2).
python3 tools/fbbt_verify.py 200 4242 | tail -1

echo "[5.75/7] Fully free LP/MIP variables + incremental API..."
gcc -O2 -march=native -I src tools/free_var_test.c src/mip.c src/fx.c src/err.c src/solver.c src/splu.c src/lu.c src/kernels.c -o /tmp/free_var_test -lm
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
  echo -n "fz_leak_test (fz_read partial-model cleanup, needs LSan): "
  gcc -std=gnu11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -I src \
      -o /tmp/fz_leak_test tools/fz_leak_test.c src/fzn.c src/mip.c src/err.c src/solver.c \
      src/splu.c src/lu.c src/kernels.c src/parser.c src/fx.c -lm
  # the accept/reject verdict is printed by the tool; the leak verdict is
  # LeakSanitizer's exit code (the tool erases its stack so dead-frame
  # pointers cannot hide a leaked partial model from the root scan)
  ASAN_OPTIONS=detect_leaks=1 /tmp/fz_leak_test >/dev/null 2>&1 \
      && echo "OK (no leaks, accept/reject correct)" \
      || { echo "FAIL"; exit 1; }
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
echo "  cumulative_exact (exact-solver fallback regression):"
if ./fznsolve examples/fzn/cumulative_exact.fzn | grep -q "=====UNKNOWN=====\|=====UNSATISFIABLE====="; then
  echo "cumulative_exact: FAIL (returned UNKNOWN/UNSAT; must find a feasible schedule)"
else
  echo "cumulative_exact: solved (expect a feasible schedule)  OK"
fi
echo -n "cumulative_verify (randomized, vs brute force): "
python3 tools/cumulative_verify.py 200 777 | sed 's/.*: //'
echo -n "cp_opt_verify (CP B&B + MIP bridge optimization, vs brute force, both paths): "
python3 tools/cp_opt_verify.py 250 20260814 | sed 's/.*: //'

echo "[8.25/8] FlatZinc strict/reified-int + float + table/circuit semantics..."
python3 tools/fzn_semantics_test.py
echo -n "table_verify (randomized, vs brute force): "
python3 tools/table_verify.py 250 20240607 | sed 's/.*: //'
echo -n "extrema_verify (randomized, vs brute force): "
python3 tools/extrema_verify.py 300 4242 | sed 's/.*: //'
echo -n "divmod_verify (trunc-div/mod + pow + sets/among + edge regressions, vs brute force): "
python3 tools/divmod_verify.py 200 20260808 | sed 's/.*: //'

# FlatZinc OUTPUT-layer round-trip (roadmap 6.7): re-parse every emitted
# byte; each printed assignment must satisfy every constraint and stay in
# its declared domain (int: exact Fractions; float: scaled 1e-6 LP
# feasibility tolerance), marker protocol exact, -a enumeration matches the
# brute-forced projection set / improving incumbents, UNSAT is
# cross-checked.  This gate caught the cp_set_vals replace-semantics
# fabrication (int_abs printed x1=-5, outside its declared domain, as
# SATISFIABLE) plus a heap-buffer-overflow in the fixed propagator.
# Discriminating: on the pre-change binary it fails 4 pins and 34/2000
# fuzzed models; post-change WRONG=0 over 19500 models (10k+4k+4k across 3
# seeds plus a 1.5k ASan/UBSan/LSan sweep).  Hard gate: rc matters.
python3 tools/fzn_output_check.py 2000 20260815 8 || { echo "fzn_output_check: FAIL"; exit 1; }

# orbit_len global (procstates 16-bit automaton, docs/PROCSTATES.md):
# 8 pins (incl. honest-decline on oversized index tables), the real
# 65536-state instance re-checked state-by-state against the independent
# C/Python ground truth (max orbit 44 at start 51641), and a 3-mode fuzz.
# Discriminating: on the pre-change binary the run at 60 fuzz models
# reports WRONG=68 (7 pin failures + the real instance + all 60 fuzzed;
# every orbit model is declined UNKNOWN);
# post-change pins OK, real instance OK, 300 fuzzed models WRONG=0 plus a
# 150-model ASan/UBSan/LSan sweep clean.  Hard gate: rc matters.
python3 tools/procstates_orbit_verify.py 120 20260816 || { echo "procstates_orbit_verify: FAIL"; exit 1; }

if command -v minizinc >/dev/null 2>&1; then
  echo "[8.5/8] MiniZinc differential (compile .mzn -> fzn -> psolve vs Gecode)..."
  python3 tools/mzn_diff.py | tail -1
  echo "[8.75/8] MiniZinc full benchmark suite..."
  python3 tools/mzn_bench.py | tail -2
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
