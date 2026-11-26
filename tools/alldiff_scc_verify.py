#!/usr/bin/env python3
"""Régin all_different filtering: solution-set differential + microbenchmark.

The CP engine's all_different propagator filters values from the residual
graph of one maximum matching (strongly connected components), matching the
construction in OR-Tools `ortools/sat/all_different.cc`.  This tool locks the
*semantics* of that optimisation down with exhaustive small-domain
differential testing: `-a` solution sets must equal a Python brute-force
oracle, including when extra disequality rows are present.

    python3 tools/alldiff_scc_verify.py [N] [seed]    # hard gate
    python3 tools/alldiff_scc_verify.py --bench       # timing table

Env: FZNSOLVE (default ./fznsolve), ALDIFF_REF (optional comparison binary).
"""
import itertools
import os
import random
import subprocess
import sys
import tempfile
import time

FZNSOLVE = os.environ.get("FZNSOLVE", "./fznsolve")


def gen(rng):
    n = rng.randint(2, 5)
    doms = []
    for _ in range(n):
        lo = rng.randint(0, 2)
        doms.append((lo, lo + rng.randint(1, 3)))
    lines = [f"var {lo}..{hi}: x{i} :: output_var;" for i, (lo, hi) in enumerate(doms)]
    lines.append("constraint all_different_int([" +
                 ",".join(f"x{i}" for i in range(n)) + "]);")
    pairs = []
    for _ in range(rng.randint(0, n)):
        i, j = rng.sample(range(n), 2)
        d = rng.randint(-2, 2)
        pairs.append((i, j, d))
        lines.append(f"constraint int_lin_ne([1,-1],[x{i},x{j}],{d});")
    lines.append("solve satisfy;")
    return "\n".join(lines) + "\n", n, doms, pairs


def solve(text, options=(), timeout=30):
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as fh:
        fh.write(text)
        path = fh.name
    try:
        return subprocess.run([FZNSOLVE, *options, path], capture_output=True,
                              text=True, timeout=timeout).stdout
    finally:
        os.unlink(path)


def solutions(out, n):
    got = set()
    for block in out.split("----------"):
        vals = {}
        for line in block.splitlines():
            line = line.strip().rstrip(";")
            if line.startswith("x") and "=" in line:
                k, v = line.split("=", 1)
                vals[k.strip()] = int(v.strip())
        if len(vals) == n:
            got.add(tuple(vals[f"x{i}"] for i in range(n)))
    return got


def verify(n, seed):
    rng = random.Random(seed)
    ok = wrong = 0
    for _ in range(n):
        text, nv, doms, pairs = gen(rng)
        got = solutions(solve(text, ("-a",)), nv)
        exp = set(t for t in itertools.product(
            *[range(lo, hi + 1) for lo, hi in doms])
            if len(set(t)) == nv and all(t[i] - t[j] != d for i, j, d in pairs))
        if got != exp:
            wrong += 1
            print("MISMATCH:", text, "missing", sorted(exp - got)[:3],
                  "extra", sorted(got - exp)[:3], file=sys.stderr)
        else:
            ok += 1
    print(f"alldiff_scc_verify: OK={ok} WRONG={wrong} (N={n}, seed={seed})")
    return 0 if wrong == 0 else 1


def bench():
    sizes = (20, 40, 60, 80, 120)
    ref = os.environ.get("ALDIFF_REF")
    print(f"{'n':>5}  {'fznsolve':>12}" + (f"  {'ref':>12}  {'speedup':>8}" if ref else ""))
    for n in sizes:
        text = (f"array [1..{n}] of var 1..{n + 1}: x;\n"
                "constraint all_different_int(x);\nsolve satisfy;\n")
        t0 = time.perf_counter()
        solve(text)
        dt = time.perf_counter() - t0
        line = f"{n:5d}  {dt * 1000:11.2f}ms"
        if ref:
            t0 = time.perf_counter()
            with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as fh:
                fh.write(text)
                path = fh.name
            try:
                subprocess.run([ref, path], capture_output=True, text=True, timeout=300)
            finally:
                os.unlink(path)
            dtr = time.perf_counter() - t0
            line += f"  {dtr * 1000:11.2f}ms  {dtr / max(dt, 1e-9):7.1f}x"
        print(line)
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--bench":
        sys.exit(bench())
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 271828
    sys.exit(verify(n, seed))
