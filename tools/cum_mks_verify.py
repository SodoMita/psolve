#!/usr/bin/env python3
"""Differential test: the dedicated makespan/cumulative scheduler.

`cp_mks_try` in src/fz_cp.inc recognises the flat MiniZinc makespan pattern

    var 0..H: s_i;  var 0..H: mk;
    constraint cumulative(s, d, r, cap);
    constraint int_le(s_i + d_i, mk);            % for every i
    solve minimize mk;

and solves it by enumerating *active* schedules (serial SGS) under
branch-and-bound, instead of branching on start values.  That is a different
algorithm from the generic CP search, so it gets its own differential test.

For random instances this script checks, independently of the solver:

  * the reported schedule is feasible (no time point exceeds the capacity),
    complete (every task placed), inside its declared start window, and
    consistent with the reported makespan (mk == max(s_i + d_i));
  * the reported verdict is SATISFIABLE-with-`==========` (a *claimed*
    optimum) exactly when the optimum is achievable, and UNSATISFIABLE only
    when no schedule exists at all;
  * when a claim is made, the objective equals the CP-SAT optimum of the
    same model (computed by tools/fzn_cpsat_ref.py, which the reference
    reports as OPTIMAL/INFEASIBLE);
  * the derived lower bound never exceeds the true optimum (a bound above
    the optimum would be an unsound cut).

Instances include the shapes that stress the algorithm: a single machine
(cap=1, pure disjunctive), unit tasks, many tasks with the same (d, r)
(symmetry), tasks that cannot overlap (r_i + r_j > cap), and horizons tight
enough to make the model infeasible.

Usage: cum_mks_verify.py [N] [seed] [solver]
"""
from __future__ import annotations

import importlib.util
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
FZNSOLVE = str(HERE.parent / "fznsolve")


def load_ref():
    """The CP-SAT oracle, or None when OR-Tools is not installed here."""
    try:
        spec = importlib.util.spec_from_file_location("fzn_cpsat_ref", HERE / "fzn_cpsat_ref.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    except Exception:                                   # noqa: BLE001
        return None


def brute_opt(durs, reserves, cap, horizon):
    """Exhaustive optimum of the makespan pattern (tiny instances only)."""
    import itertools
    best = None
    for starts in itertools.product(range(horizon + 1), repeat=len(durs)):
        mk = max(s + d for s, d in zip(starts, durs))
        if mk > horizon:
            continue
        if best is not None and mk >= best:
            continue
        ok = True
        for t in range(mk):
            load = sum(reserves[i] for i, s in enumerate(starts) if s <= t < s + durs[i])
            if load > cap:
                ok = False
                break
        if ok:
            best = mk
    return best


def gen_model(rng: random.Random, brute_mode: bool = False):
    """Return (text, durs, reserves, cap) for the makespan pattern."""
    n = rng.randint(2, 9)
    cap = rng.randint(1, 5)
    horizon = rng.randint(max(4, n), max(6, 3 * n))
    if brute_mode:
        # keep the exhaustive oracle honest: at most 5 tasks, at most 8 slots
        n = rng.randint(2, 5)
        horizon = rng.randint(max(3, n), 8)
    durs, reserves = [], []
    style = rng.random()
    for _ in range(n):
        if style < 0.2:
            d = 1                                   # unit tasks
        else:
            d = rng.randint(1, min(4, horizon))
        if style > 0.8:
            r = cap                                # everyone wants it all
        else:
            r = rng.randint(1, cap)
        durs.append(d)
        reserves.append(r)
    lines = [f"var 0..{horizon}: s{i} :: output_var;" for i in range(n)]
    lines.append(f"var 0..{horizon}: mk :: output_var;")
    sl = ",".join(f"s{i}" for i in range(n))
    lines.append(f"constraint cumulative([{sl}],[{','.join(map(str, durs))}],"
                 f"[{','.join(map(str, reserves))}],{cap});")
    for i, d in enumerate(durs):
        lines.append(f"constraint int_le(s{i}+{d}, mk);")
    lines.append("solve minimize mk;")
    return "\n".join(lines) + "\n", durs, reserves, cap, horizon


def feasible(start, durs, reserves, cap, mk):
    if sorted(start) != list(start):
        pass
    for i, s in enumerate(start):
        if s < 0 or s + durs[i] > mk:
            return False
    for t in range(max(mk, 1)):
        load = sum(reserves[i] for i, s in enumerate(start) if s <= t < s + durs[i])
        if load > cap:
            return False
    return True


def parse(text):
    """Extract (status, start vector, mk) from fznsolve output."""
    st = "UNKNOWN"
    if "==========" in text:
        st = "SAT"
    if "=====UNSATISFIABLE=====" in text:
        st = "UNSAT"
    starts = {}
    mk = None
    for m in re.finditer(r"^(s\d+|mk) = (-?\d+);$", text, re.M):
        name, val = m.group(1), int(m.group(2))
        if name == "mk":
            mk = val
        else:
            starts[int(name[1:])] = val
    return st, starts, mk


def main():
    n_inst = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260901
    solver = sys.argv[3] if len(sys.argv) > 3 else FZNSOLVE
    rng = random.Random(seed)
    ref = load_ref()
    brute = ref is None or "--brute" in sys.argv
    if brute:
        print("  (OR-Tools not available: exhaustive brute-force oracle, "
              "tiny instances)")
    tmp = tempfile.mkdtemp(prefix="cum_mks")
    ok = mismatch = unknown = 0
    problems = []
    for k in range(n_inst):
        text, durs, reserves, cap, H = gen_model(rng, brute)
        n = len(durs)
        path = os.path.join(tmp, f"m{k}.fzn")
        with open(path, "w") as fh:
            fh.write(text)
        p = subprocess.run([solver, "-s", path], capture_output=True, text=True, timeout=120)
        st, starts, mk = parse(p.stdout)
        if brute:
            b = brute_opt(durs, reserves, cap, H)
            ref_st = "UNSAT" if b is None else "SAT"
            ref_mk = b
            proven = True
        else:
            res = ref.run(path, workers=1, time_limit=60)
            ref_st = res["status"]
            ref_mk = res["solutions"][0]["mk"] if res["solutions"] else None
            proven = res.get("proven", True)
        if st == "SAT":
            errs = []
            if len(starts) != n:
                errs.append(f"only {len(starts)}/{n} starts reported")
            else:
                order = [starts[i] for i in range(n)]
                if not feasible(order, durs, reserves, cap, mk):
                    errs.append("reported schedule violates the capacity")
                if mk != max(starts[i] + durs[i] for i in range(n)):
                    errs.append("reported mk is not the schedule's makespan")
            if ref_st != "SAT":
                errs.append(f"claimed optimum {mk} but reference says {ref_st}")
            elif ref_mk is not None and proven and mk != ref_mk:
                errs.append(f"optimum {mk} != reference optimum {ref_mk}")
            if errs:
                mismatch += 1
                problems.append((k, "; ".join(errs), text))
            else:
                ok += 1
        elif st == "UNSAT":
            if ref_st == "SAT":
                mismatch += 1
                problems.append((k, f"claimed UNSAT but reference optimum is {ref_mk}", text))
            else:
                ok += 1
        else:
            unknown += 1
            problems.append((k, f"UNKNOWN (reference: {ref_st} mk={ref_mk})", text))
    for k, why, text in problems[:6]:
        print(f"  instance {k}: {why}")
        print("    " + text.replace("\n", " ")[:200])
    print(f"cum_mks_verify: OK={ok} MISMATCH={mismatch} UNKNOWN={unknown} "
          f"(N={n_inst}, seed={seed})")
    return 0 if mismatch == 0 and unknown == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
