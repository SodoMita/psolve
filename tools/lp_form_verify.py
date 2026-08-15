#!/usr/bin/env python3
"""Differential + regression tests for LP input hygiene (external audit F-1/F-2).

F-1: a variable with l > u makes every LP trivially INFEASIBLE, yet the solver
used to skip the stuck variable in its entering rules and could ride a ray on
another variable to a fabricated UNBOUNDED (a status that implies feasibility,
i.e. a wrong answer).  Fixed by an up-front empty-box INFEASIBLE verdict plus
the primal-feasibility certificate now gating UNBOUNDED.

F-2: duplicate (row,col) triplets were accepted but treated inconsistently
(matvecs summed both entries, column reads kept one), degrading legitimate
LPs to NUMERICAL_FAILURE.  Both the text parser and solver_create now merge
duplicates by summing (GLPK semantics).

Every generated case is cross-checked against an independent continuous
reference (scipy's HiGHS linprog; continuous LPs are NOT enumerable over an
integer grid), and every fixed-point regression reproduces the audit
reproducers.  Run with LPSOLVE=... to prove discrimination against a
pre-fix binary.

Usage: lp_form_verify.py [N] [seed]
"""
from __future__ import annotations

import itertools
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
LPSOLVE = pathlib.Path(os.environ.get("LPSOLVE", str(ROOT / "lpsolve")))


def run_lp(text: str):
    with tempfile.NamedTemporaryFile("w", suffix=".lp", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        r = subprocess.run([str(LPSOLVE), path], text=True, capture_output=True, timeout=20)
    finally:
        os.unlink(path)
    out = r.stdout
    m = re.search(r"status: (\w+)", out)
    status = m.group(1) if m else "PARSE_ERROR"
    mo = re.search(r"objective: (-?[\d.eE+-]+)", out)
    obj = float(mo.group(1)) if mo else None
    return status, obj, out


def lp_text(n, m, c, b, rel, bounds, triplets, maximize):
    L = ["maximize" if maximize else "minimize", f"{n} {m}",
         " ".join(repr(v) for v in c)]
    if m:
        L.append(" ".join(repr(v) for v in b))
        L.append("".join(rel))
    for lo, hi in bounds:
        L.append(f"{repr(lo)} {repr(hi)}")
    L.append(str(len(triplets)))
    for r, cc, v in triplets:
        L.append(f"{r} {cc} {repr(v)}")
    return "\n".join(L) + "\n"


def brute_force(n, m, c, b, rel, bounds, triplets, maximize, grid):
    """Continuous reference via scipy/HiGHS (grid arg kept for signature
    stability; continuous LPs are NOT enumerable over an integer grid --
    2x<=1, 2x>=1 has no integer point yet is feasible).  Duplicated
    triplets are merged by summation, exactly the documented semantics."""
    from scipy.optimize import linprog
    import numpy as np
    A = [[0.0] * n for _ in range(m)]
    for r, cc, v in triplets:
        A[r][cc] += v
    A_ub, b_ub, A_eq, b_eq = [], [], [], []
    for i in range(m):
        if rel[i] == "<":
            A_ub.append(A[i]); b_ub.append(b[i])
        elif rel[i] == ">":
            A_ub.append([-a for a in A[i]]); b_ub.append(-b[i])
        else:
            A_eq.append(A[i]); b_eq.append(b[i])
    res = linprog(
        np.array(c) * (-1.0 if maximize else 1.0),
        A_ub=np.array(A_ub) if A_ub else None,
        b_ub=np.array(b_ub) if b_ub else None,
        A_eq=np.array(A_eq) if A_eq else None,
        b_eq=np.array(b_eq) if b_eq else None,
        bounds=[(None if lo <= -1e29 else lo, None if hi >= 1e29 else hi) for lo, hi in bounds],
        method="highs",
    )
    if res.status == 2:
        return None
    if res.status != 0:
        return "REVIEW"      # reference inconclusive; skip the case honestly
    return float(res.fun) * (-1.0 if maximize else 1.0)


def gen_lp(rng, force_contra=False, force_dups=False):
    n = rng.randint(1, 3)
    m = rng.randint(0, 3)
    c = [rng.choice([-2, -1, 1, 2]) for _ in range(n)]
    b = [rng.randint(-4, 6) for _ in range(m)]
    rel = [rng.choice("<>=") for _ in range(m)]
    bounds = []
    contra_at = rng.randrange(n) if force_contra else -1
    for j in range(n):
        if j == contra_at:
            hi = rng.randint(-2, 1)
            lo = hi + rng.randint(1, 3)          # strictly l > u
        else:
            lo = rng.choice([-3, 0])
            hi = lo + rng.randint(1, 4)
        bounds.append((float(lo), float(hi)))
    ntr = rng.randint(1, 5) if m else 0
    triplets = []
    for _ in range(ntr):
        r = rng.randrange(m); cc = rng.randrange(n)
        v = float(rng.choice([-2, -1, 1, 2]))
        triplets.append((r, cc, v))
        if force_dups and rng.random() < 0.6:
            triplets.append((r, cc, float(rng.choice([-2, -1, 1, 2]))))
    maximize = rng.random() < 0.5
    return n, m, c, b, rel, bounds, triplets, maximize


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 150
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(seed)
    grid = list(range(-3, 4))
    ok = wrong = 0

    # --- fixed-point regressions: the audit reproducers ----------------------
    st, obj, out = run_lp("maximize\n2 0\n1 1\n\n5 2\n-inf inf\n0\n")
    if st != "INFEASIBLE":
        print(f"[repro-F1] contradictory bounds must be INFEASIBLE, got {st}:\n{out}")
        wrong += 1
    else:
        ok += 1
    st, obj, out = run_lp("maximize\n1 1\n1\n4\n<\n0 inf\n2\n0 0 1\n0 0 1\n")
    if st != "OPTIMAL" or obj is None or abs(obj - 2.0) > 1e-9:
        print(f"[repro-F2] merged duplicate triplet must be OPTIMAL obj=2, got {st} obj={obj}:\n{out}")
        wrong += 1
    else:
        ok += 1

    # --- generated contradictory-bounds LPs: always INFEASIBLE ---------------
    for t in range(N):
        lp = gen_lp(rng, force_contra=True)
        st, obj, out = run_lp(lp_text(*lp))
        if st != "INFEASIBLE":
            print(f"[{t}] l>u bounds ({lp[5][0]}...) must be INFEASIBLE, got {st}:\n{out}")
            wrong += 1
        else:
            ok += 1

    # --- generated duplicate-triplet LPs: status+objective vs reference ----
    review = 0
    for t in range(N):
        lp = gen_lp(rng, force_dups=True)
        st, obj, out = run_lp(lp_text(*lp))
        best = brute_force(*lp, grid=grid)
        if best == "REVIEW":
            review += 1
            continue
        if best is None:
            if st != "INFEASIBLE":
                print(f"[{t}] dup-triplet LP (reference infeasible) but solver said {st}:\n{out}")
                wrong += 1
                continue
            ok += 1
            continue
        if st != "OPTIMAL":
            print(f"[{t}] dup-triplet LP must be OPTIMAL (ref={best}), got {st}:\n{out}")
            wrong += 1
            continue
        if obj is None or abs(obj - best) > 1e-6:
            print(f"[{t}] dup-triplet LP wrong objective solver={obj} ref={best}:\n{out}")
            wrong += 1
            continue
        ok += 1
    if review:
        print(f"  (reference inconclusive on {review} cases; skipped)")

    print(f"lp_form_verify: OK={ok} WRONG={wrong}  (N={N}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    raise SystemExit(main())
