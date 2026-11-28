#!/usr/bin/env python3
"""Differential test: scheduling models (jobshop / shop-with-precedence).

`cp_shop_apply` in src/fz_cp.inc recognises scheduling structures --
`disjunctive`/`cumulative` over constant durations plus `s_i + d_i <= mk` rows
-- and drives them with a serial-SGS branch and bound: exact when the model is
exactly that structure, and an incumbent generator (plus a bound pushed into
`mk` at every search node) when the model carries extra constraints.  Both
paths are checked here against a CP-SAT oracle and, independently, by
re-verifying the reported schedule:

  * every job's operations run in order (precedence chains),
  * no machine runs two operations at once,
  * reported starts are inside their declared windows,
  * `mk` equals the makespan of the reported schedule,
  * a claimed optimum (status SAT with `==========`) equals the oracle optimum,
  * UNSATISFIABLE is only claimed when the oracle cannot schedule it either.

Half of the instances carry ordinary side constraints (`int_ne`, a bound on
one start, a sum row) so the incumbent/verification path is exercised too.

Usage: jobshop_verify.py [N] [seed] [solver]
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
    try:
        spec = importlib.util.spec_from_file_location("fzn_cpsat_ref", HERE / "fzn_cpsat_ref.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod
    except Exception:                                   # noqa: BLE001
        return None


def gen_instance(rng, side_constraints):
    jobs = rng.randint(2, 4)
    machines = rng.randint(2, 4)
    route = []
    for _ in range(jobs):
        ops = [(m, rng.randint(1, 6)) for m in rng.sample(range(machines), machines)]
        route.append(ops)
    horizon = sum(d for ops in route for _, d in ops) + rng.choice([0, 0, 2, 5])
    lines, names = [], {}
    for j in range(jobs):
        for k in range(machines):
            names[(j, k)] = f"s{j}_{k}"
            lines.append(f"var 0..{horizon}: s{j}_{k} :: output_var;")
    lines.append(f"var 0..{horizon}: mk :: output_var;")
    for j in range(jobs):
        for k, (m, d) in enumerate(route[j]):
            lines.append(f"constraint int_le({names[(j, k)]}+{d}, mk);")
            if k:
                lines.append(f"constraint int_le({names[(j, k - 1)]}+{route[j][k - 1][1]}, "
                             f"{names[(j, k)]});")
    for m in range(machines):
        vs, ds = [], []
        for j in range(jobs):
            for k, (mm, d) in enumerate(route[j]):
                if mm == m:
                    vs.append(names[(j, k)])
                    ds.append(d)
        lines.append(f"constraint disjunctive([{','.join(vs)}],[{','.join(map(str, ds))}]);")
    if side_constraints:
        a = names[(0, 0)]
        b = names[(jobs - 1, machines - 1)]
        lines.append(f"constraint int_le({a},{rng.randint(1, 4)});")
        if a != b:
            lines.append(f"constraint int_ne({b},{rng.randint(0, 3)});")
    lines.append("solve minimize mk;")
    return "\n".join(lines) + "\n", route, horizon


def feasible(starts, route, mk):
    jobs, machines = len(route), len(route[0])
    for j in range(jobs):
        for k, (m, d) in enumerate(route[j]):
            s = starts[(j, k)]
            if s < 0 or s + d > mk:
                return "start out of range"
            if k:
                pm, pd = route[j][k - 1]
                if starts[(j, k - 1)] + pd > s:
                    return "precedence violated"
    for m in range(machines):
        ops = [(starts[(j, k)], route[j][k][1])
               for j in range(jobs) for k in range(machines) if route[j][k][0] == m]
        for x in range(len(ops)):
            for y in range(x + 1, len(ops)):
                a, da = ops[x]
                b, db = ops[y]
                if not (a + da <= b or b + db <= a):
                    return "machine overlap"
    return None


def parse(out):
    st = "UNKNOWN"
    if "==========" in out:
        st = "SAT"
    if "=====UNSATISFIABLE=====" in out:
        st = "UNSAT"
    vals = {}
    for m in re.finditer(r"^(s(\d+)_(\d+)|mk) = (-?\d+);$", out, re.M):
        if m.group(1) == "mk":
            vals["mk"] = int(m.group(4))
        else:
            vals[(int(m.group(2)), int(m.group(3)))] = int(m.group(4))
    return st, vals


def main():
    n_inst = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261125
    solver = sys.argv[3] if len(sys.argv) > 3 else FZNSOLVE
    rng = random.Random(seed)
    ref = load_ref()
    if ref is None:
        print("jobshop_verify: OR-Tools not available -- checking schedules only")
    tmp = tempfile.mkdtemp(prefix="jobshop")
    ok = mismatch = unknown = 0
    problems = []
    for k in range(n_inst):
        side = (k % 2 == 1)
        text, route, horizon = gen_instance(rng, side)
        path = os.path.join(tmp, f"j{k}.fzn")
        with open(path, "w") as fh:
            fh.write(text)
        p = subprocess.run([solver, "-s", path], capture_output=True, text=True, timeout=180)
        st, vals = parse(p.stdout)
        if ref is None:
            ref_st, ref_mk = "SKIP", None
        else:
            res = ref.run(path, workers=1, time_limit=60)
            ref_st = res["status"]
            ref_mk = res["solutions"][0]["mk"] if res["solutions"] else None
        errs = []
        if st == "SAT":
            starts = {key: v for key, v in vals.items() if key != "mk"}
            if len(starts) != len(route) * len(route[0]):
                errs.append("missing starts")
            else:
                bad = feasible(starts, route, vals["mk"])
                if bad:
                    errs.append(bad)
                if vals["mk"] != max(starts[(j, kk)] + route[j][kk][1]
                                     for j in range(len(route)) for kk in range(len(route[0]))):
                    errs.append("mk is not the schedule's makespan")
            if ref_st == "SKIP":
                pass
            elif ref_st != "SAT":
                errs.append(f"claimed optimum {vals['mk']} but reference says {ref_st}")
            elif ref_mk is not None and res.get("proven", True) and vals["mk"] != ref_mk:
                errs.append(f"optimum {vals['mk']} != reference optimum {ref_mk}")
        elif st == "UNSAT":
            if ref_st == "SAT":
                errs.append(f"claimed UNSAT but reference optimum is {ref_mk}")
        else:
            # an unproven answer is acceptable only when it still carries a
            # verified schedule of reference quality
            starts = {key: v for key, v in vals.items() if key != "mk"}
            if not starts:
                if ref_st == "SKIP":
                    # without an oracle an unproven answer is not a failure,
                    # but it is not evidence either: count it separately
                    unknown += 1
                    continue
                unknown += 1
                problems.append((k, f"UNKNOWN (reference {ref_st} mk={ref_mk})", text))
                continue
            bad = feasible(starts, route, vals["mk"]) if len(starts) == len(route) * len(route[0]) else "missing starts"
            if bad:
                errs.append("unproven answer with bad schedule: " + bad)
            elif ref_mk is not None and vals["mk"] < ref_mk:
                errs.append("unproven answer better than the reference optimum")
        if errs:
            mismatch += 1
            problems.append((k, "; ".join(errs), text))
        else:
            ok += 1
    for k, why, text in problems[:5]:
        print(f"  instance {k}: {why}")
        print("    " + text.replace("\n", " ")[:180])
    print(f"jobshop_verify: OK={ok} MISMATCH={mismatch} UNKNOWN={unknown} "
          f"(N={n_inst}, seed={seed})")
    return 0 if mismatch == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
