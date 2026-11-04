#!/usr/bin/env python3
"""Performance parity benchmark: fznsolve vs the bundled fzn-cp-sat and
fzn-gecode oracles.

For every examples/mzn/*.mzn model, compile twice with MiniZinc
(`--solver cp-sat` and `--solver gecode`), then run each of
  ./fznsolve, fzn-cp-sat, fzn-gecode
on the emitted .fzn with a wall-clock timeout.  Records status and solve
time; reports per-oracle solved counts, geomean/median time ratios and the
worst regressions (fznsolve timeout/slow while the oracle solved).

Usage: fzn_cpsat_bench.py [timeout_s] [max_models]
Env: FZNSOLVE, MINIZINC, FZN_CP_SAT, FZN_GECODE, MZ_LIB (LD_LIBRARY_PATH).
"""
import glob
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
FZ = os.environ.get("FZNSOLVE", os.path.join(HERE, "..", "fznsolve"))
MINIZINC = os.environ.get("MINIZINC", "/home/user/mz/bin/minizinc")
FZN_CP_SAT = os.environ.get("FZN_CP_SAT", "/home/user/mz/bin/fzn-cp-sat")
FZN_GECODE = os.environ.get("FZN_GECODE", "/home/user/mz/bin/fzn-gecode")
MZ_LIB = os.environ.get("MZ_LIB", "/home/user/mz/lib")
EXAMPLES = os.path.join(HERE, "..", "examples", "mzn")


def run_timed(cmd, timeout):
    t0 = time.perf_counter()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           env={**os.environ, "LD_LIBRARY_PATH": MZ_LIB})
        dt = time.perf_counter() - t0
        out = p.stdout + p.stderr
        if "=====UNSATISFIABLE=====" in out:
            st = "UNSAT"
        elif "=====UNKNOWN=====" in out or "=====ERROR=====" in out:
            st = "UNKNOWN" if "=====UNKNOWN=====" in out else "ERROR"
        elif "----------" in out or "==========" in out:
            st = "SAT"
        else:
            st = "?"
        return st, dt
    except subprocess.TimeoutExpired:
        return "TIMEOUT", time.perf_counter() - t0


def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    max_models = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
    models = sorted(glob.glob(os.path.join(EXAMPLES, "*.mzn")))[:max_models]
    tmp = tempfile.mkdtemp(prefix="fzn_cpsat_bench")
    rows = []
    try:
        for i, mzn in enumerate(models):
            base = os.path.basename(mzn)[:-4]
            for solver in ("cp-sat", "gecode"):
                fzn = os.path.join(tmp, f"{base}.{solver}.fzn")
                c = subprocess.run([MINIZINC, "-c", "--solver", solver, mzn,
                                    "--fzn", fzn], capture_output=True, text=True, timeout=120)
                if c.returncode != 0 or not os.path.exists(fzn):
                    continue
                with open(fzn) as fh:
                    text = fh.read()
                # skip float models (out of scope for the int parity claim)
                if re.search(r"^\s*(var\s+float|float)", text, re.M):
                    continue
                row = {"model": base, "solver_lib": solver}
                for name, exe in (("fznsolve", FZ), ("cp-sat", FZN_CP_SAT), ("gecode", FZN_GECODE)):
                    st, dt = run_timed([exe, fzn], timeout)
                    row[name] = {"st": st, "t": round(dt, 4)}
                rows.append(row)
                print(f"[{i+1}/{len(models)}] {base} ({solver}): " +
                      " ".join(f"{k}={row[k]['st']}/{row[k]['t']:.2f}s" for k in ("fznsolve", "cp-sat", "gecode")),
                      flush=True)
    finally:
        pass
    out_json = os.path.join(HERE, "fzn_cpsat_bench_results.json")
    with open(out_json, "w") as fh:
        json.dump(rows, fh, indent=1)

    # summary per oracle
    print("\n=== summary (fznsolve vs oracle on the same .fzn) ===")
    for oracle in ("cp-sat", "gecode"):
        both_solved = [(r["fznsolve"], r[oracle]) for r in rows
                       if r["fznsolve"]["st"] in ("SAT", "UNSAT") and r[oracle]["st"] in ("SAT", "UNSAT")]
        agree = [p for p in both_solved if p[0]["st"] == p[1]["st"]]
        ratios = [p[0]["t"] / max(p[1]["t"], 1e-4) for p in agree]
        fz_ok = sum(1 for r in rows if r["fznsolve"]["st"] in ("SAT", "UNSAT"))
        or_ok = sum(1 for r in rows if r[oracle]["st"] in ("SAT", "UNSAT"))
        print(f"vs {oracle}: models={len(rows)} fznsolve_solved={fz_ok} oracle_solved={or_ok} "
              f"both={len(both_solved)} verdict_agree={len(agree)}")
        if ratios:
            print(f"  time ratio fznsolve/{oracle}: geomean={math.exp(sum(map(math.log, ratios))/len(ratios)):.2f}x "
                  f"median={statistics.median(ratios):.2f}x max={max(ratios):.2f}x")
        # regressions: oracle solved, fznsolve did not (or much slower)
        regs = [(r["model"], r["solver_lib"], r[oracle], r["fznsolve"]) for r in rows
                if r[oracle]["st"] in ("SAT", "UNSAT") and r["fznsolve"]["st"] not in ("SAT", "UNSAT")]
        for m, sl, o, f in regs[:20]:
            print(f"  REGRESSION {m} ({sl}): oracle={o['st']}/{o['t']:.2f}s fznsolve={f['st']}/{f['t']:.2f}s")
        slow = sorted(((r["fznsolve"]["t"] / max(r[oracle]["t"], 1e-4), r["model"], r["solver_lib"],
                        r["fznsolve"], r[oracle]) for r in rows
                       if r["fznsolve"]["st"] in ("SAT", "UNSAT") and r[oracle]["st"] in ("SAT", "UNSAT")
                       and r["fznsolve"]["st"] == r[oracle]["st"]), reverse=True)[:10]
        for ratio, m, sl, f, o in slow:
            if ratio > 3:
                print(f"  SLOW {m} ({sl}): ratio={ratio:.1f}x fznsolve={f['st']}/{f['t']:.2f}s oracle={o['st']}/{o['t']:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
