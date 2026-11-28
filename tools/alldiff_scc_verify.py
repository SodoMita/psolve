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


def gen(rng, affine=True):
    """Random all_different model.

    With `affine` the arguments are shifted (`x0+2`, `x1-1`, ...), which is how
    MiniZinc flattens n-queens/magic-square diagonals; the propagator must
    respect those shifts exactly.
    """
    n = rng.randint(2, 5)
    doms = []
    for _ in range(n):
        lo = rng.randint(0, 2)
        doms.append((lo, lo + rng.randint(1, 3)))
    offs = [rng.randint(-2, 2) if affine else 0 for _ in range(n)]
    lines = [f"var {lo}..{hi}: x{i} :: output_var;" for i, (lo, hi) in enumerate(doms)]
    args = []
    for i, k in enumerate(offs):
        if k > 0:
            args.append(f"x{i}+{k}")
        elif k < 0:
            args.append(f"x{i}-{-k}")
        else:
            args.append(f"x{i}" if not affine else f"x{i}+0")
    lines.append("constraint all_different_int([" + ",".join(args) + "]);")
    pairs = []
    for _ in range(rng.randint(0, n)):
        i, j = rng.sample(range(n), 2)
        d = rng.randint(-2, 2)
        pairs.append((i, j, d))
        lines.append(f"constraint int_lin_ne([1,-1],[x{i},x{j}],{d});")
    lines.append("solve satisfy;")
    return "\n".join(lines) + "\n", n, doms, pairs, offs


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


def gen_pure(rng):
    """Pure all_different model: nothing but disjoint all_different components.

    This is the shape `cp_alldiff_apply` solves as a bipartite matching (one
    Kuhn run per component gives the witness, Hall gives the UNSAT proof), so
    it is generated separately: single component, several disjoint components,
    constant members, and a pigeonhole case that must come back UNSAT.
    """
    ncomp = rng.choice([1, 1, 2, 3])
    lines, doms, comps = [], [], []
    nxt = 0
    for _ in range(ncomp):
        n = rng.randint(2, 4)
        lo = rng.randint(0, 2)
        hi = lo + rng.randint(1, 4)
        comp = []
        for _ in range(n):
            lines.append(f"var {lo}..{hi}: v{nxt} :: output_var;")
            doms.append((lo, hi))
            comp.append(nxt)
            nxt += 1
        consts = []
        if rng.random() < 0.3:
            consts = [rng.randint(lo, hi) for _ in range(rng.randint(1, 2))]
        args = [f"v{i}" for i in comp] + [str(c) for c in consts]
        rng.shuffle(args)
        lines.append("constraint all_different_int([" + ",".join(args) + "]);")
        comps.append((comp, consts))
    if rng.random() < 0.25:                    # pigeonhole: more vars than values
        n = rng.randint(3, 5)
        hi = n - 2
        comp = []
        for _ in range(n):
            lines.append(f"var 0..{hi}: v{nxt} :: output_var;")
            doms.append((0, hi))
            comp.append(nxt)
            nxt += 1
        lines.append("constraint all_different_int([" +
                     ",".join(f"v{i}" for i in comp) + "]);")
        comps.append((comp, []))
    lines.append("solve satisfy;")
    return "\n".join(lines) + "\n", doms, comps, [f"v{i}" for i in range(nxt)]


def solve_one(text, timeout=60):
    out = solve(text, (), timeout=timeout)
    if "=====UNSATISFIABLE=====" in out:
        return "UNSAT", {}
    vals = {}
    for block in out.split("----------"):
        for line in block.splitlines():
            line = line.strip().rstrip(";")
            if "=" in line and line.split("=")[0].strip().startswith("v"):
                k, v = line.split("=", 1)
                try:
                    vals[k.strip()] = int(v.strip())
                except ValueError:
                    pass
    return ("SAT" if vals else "UNKNOWN"), vals


def verify_match(n, seed):
    """Differential test of the pure-matching path (no `-a`), vs brute force."""
    rng = random.Random(seed)
    ok = wrong = 0
    for _ in range(n):
        text, doms, comps, names = gen_pure(rng)
        st, vals = solve_one(text)
        witness = None
        for t in itertools.product(*[range(lo, hi + 1) for lo, hi in doms]):
            good = True
            for comp, consts in comps:
                seen = set(consts)
                if len(seen) != len(consts):
                    good = False
                    break
                for i in comp:
                    if t[i] in seen:
                        good = False
                        break
                    seen.add(t[i])
                if not good:
                    break
            if good:
                witness = t
                break
        exp = "SAT" if witness is not None else "UNSAT"
        bad = None
        if st != exp:
            bad = f"verdict {st} vs {exp}"
        elif st == "SAT":
            got = [vals.get(nm) for nm in names]
            if any(v is None for v in got):
                bad = "incomplete assignment"
            else:
                for comp, consts in comps:
                    seen = set(consts)
                    if len(seen) != len(consts):
                        bad = "duplicate constants"
                        break
                    for i in comp:
                        if got[i] in seen:
                            bad = "all_different violated in the witness"
                            break
                        seen.add(got[i])
                    if bad:
                        break
                if not bad:
                    for i, (lo, hi) in enumerate(doms):
                        if not (lo <= got[i] <= hi):
                            bad = "value outside its domain"
                            break
        if bad:
            wrong += 1
            print("MATCH MISMATCH:", bad, "\n", text, file=sys.stderr)
        else:
            ok += 1
    print(f"alldiff_match_verify: OK={ok} WRONG={wrong} (N={n}, seed={seed})")
    return ok, wrong


def verify(n, seed):
    rng = random.Random(seed)
    ok = wrong = 0
    for _ in range(n):
        text, nv, doms, pairs, offs = gen(rng)
        got = solutions(solve(text, ("-a",)), nv)
        exp = set(t for t in itertools.product(
            *[range(lo, hi + 1) for lo, hi in doms])
            if len({t[i] + offs[i] for i in range(nv)}) == nv
            and all(t[i] - t[j] != d for i, j, d in pairs))
        if got != exp:
            wrong += 1
            print("MISMATCH:", text, "missing", sorted(exp - got)[:3],
                  "extra", sorted(got - exp)[:3], file=sys.stderr)
        else:
            ok += 1
    print(f"alldiff_scc_verify: OK={ok} WRONG={wrong} (N={n}, seed={seed})")
    mok, mwrong = verify_match(max(20, n // 2), seed + 1)
    print(f"alldiff_scc_verify: total OK={ok + mok} WRONG={wrong + mwrong}")
    return 0 if wrong == 0 and mwrong == 0 else 1


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
