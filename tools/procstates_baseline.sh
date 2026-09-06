#!/usr/bin/env bash
# Baseline: stock MiniZinc solvers vs psolve on the procstates orbit problem
# (Unesty/Doing automaton, longest functional-graph orbit; ground truth 44).
# Usage: procstates_baseline.sh [LIMIT_MS]     (default 600000 = 10 min)
set -u
LIMIT="${1:-600000}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MZN_DIR=/tmp/MiniZincIDE-2.9.4-bundle-linux-x86_64
export PATH="$MZN_DIR/bin:$PATH"
export LD_LIBRARY_PATH="$MZN_DIR/lib"
MODEL=$REPO/examples/procstates/procstates_orbit_stock.mzn
DZN=$REPO/examples/procstates/stock_h64.dzn

if [ ! -f "$DZN" ]; then
  echo "missing $DZN (generated alongside procstates_next.dzn)" >&2
  exit 1
fi

for S in gecode chuffed cp-sat; do
  echo "=== $S (limit ${LIMIT}ms, portable bounded encoding H=64) ==="
  timeout "$((LIMIT / 1000 + 120))" minizinc --solver "$S" --time-limit "$LIMIT" \
      --statistics "$MODEL" "$DZN" > /tmp/baseline_$S.out 2>&1
  rc=$?
  echo "rc=$rc"
  grep -E "^(len|start) =|%%%mzn-stat: (solveTime|nFailures|status)|==========" /tmp/baseline_$S.out | head -6
  echo
done

echo "=== psolve (native orbit_len global) ==="
T0=$(date +%s%N)
"$REPO/fznsolve" "$REPO/examples/procstates/procstates_orbit.fzn"
T1=$(date +%s%N)
echo "wall: $(( (T1 - T0) / 1000000 )) ms"
