#!/usr/bin/env python3
"""Brute-force differential verifier for FlatZinc ``int_pow(x, y, z)``.

MiniZinc 2.10.1 semantics (verified against Gecode 6.4.0, and against
docs.minizinc.dev/en/stable/lib-flatzinc-int.html)::

    z = x^y                       when y >= 0   (with 0^0 = 1)
    z = 1 div pow(x, abs(y))      when y < 0    (div truncates toward zero)

For y < 0 the result is therefore:

    x == 1              -> 1
    x == -1             -> (-1)^abs(y)   (1 div -1 = -1, 1 div 1 = 1)
    |x| >= 2            -> 0             (|denominator| >= 2)
    x == 0              -> UNDEFINED     (division by zero)

Real ``mzn2fzn`` never emits that undefined cell: the stdlib ``pow_t``
wrapper adds a totality guard ``x != 0 \\/ y >= 0``.  This verifier mirrors
that by excluding (0, negative) lattice points from the reference model.
Hand-written .fzn files without the guard simply get fewer solutions, which
is still a valid (never fabricated) verdict.

The verifier generates raw .fzn files with a VARIABLE exponent, runs
./fznsolve -a, and compares the exact solution set (or the optimum for
optimize models) against brute-force enumeration of the lattice.  It covers
positive/negative bases, negative exponents, wide/inexact powers, extra
constraints on x/y/z, UNSAT instances, and optimize models.

Usage: pow_var_verify.py [N] [seed]
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

FZ = "./fznsolve"


def run_fzn(text, all_solutions=True):
    fd, path = tempfile.mkstemp(suffix=".fzn")
    try:
        os.write(fd, text.encode())
        os.close(fd)
        cmd = [FZ, "-a", path] if all_solutions else [FZ, path]
        p = subprocess.run(cmd, capture_output=True, timeout=60)
        return p.stdout.decode()
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        os.unlink(path)


def reference_pow(base, exp):
    """Return z per MiniZinc int_pow, or None for the undefined cell."""
    if exp >= 0:
        # Python's ** matches MiniZinc for exp>=0 including 0**0 == 1.
        return base ** exp
    if base == 0:
        return None
    if base == 1:
        return 1
    if base == -1:
        return -1 if ((-exp) & 1) else 1
    return 0  # |base| >= 2 -> 1 div (|denom|>=2) == 0


def parse_solutions(out):
    sols = []
    for blk in out.split("----------"):
        vals = {}
        for k, v in re.findall(r"^(\w+) = (-?\d+);", blk, re.M):
            vals[k] = int(v)
        if vals:
            sols.append(vals)
    return sols


def gen_case(rng):
    """Return (text, ref_solutions_set_of_tuples, opt) for one random model.

    opt is None for satisfy, else ('min'|'max', brute_objective).
    """
    # Small bounded domains; keep the -a enumeration tiny.
    xlo, xhi = rng.choice([(-3, 3), (0, 4), (-2, 2), (-1, 4), (1, 4), (-4, 0)])
    ylo, yhi = rng.choice([(-3, 3), (0, 3), (1, 3), (-2, 2), (-4, 1)])
    zlo, zhi = rng.choice([(-30, 30), (0, 64), (-100, 100), (-10, 10), (0, 20)])

    lines = [
        f"var {xlo}..{xhi}: x :: output_var;",
        f"var {ylo}..{yhi}: y :: output_var;",
        f"var {zlo}..{zhi}: z :: output_var;",
        "constraint int_pow(x,y,z);",
    ]

    # Optional extra relations to make instances tighter / infeasible.
    extra = []
    fx = fy = fz = None
    roll = rng.random()
    if roll < 0.25:
        fx = rng.randint(xlo, xhi)
        lines.append(f"constraint int_eq(x, {fx});")
    elif roll < 0.45:
        fy = rng.randint(ylo, yhi)
        lines.append(f"constraint int_eq(y, {fy});")
    elif roll < 0.60:
        fz = rng.randint(zlo, zhi)
        lines.append(f"constraint int_eq(z, {fz});")
    elif roll < 0.72:
        t = rng.randint(zlo, zhi)
        lines.append(f"constraint int_lin_le([1], [z], {t});")

    opt = None
    if rng.random() < 0.35:
        opt = rng.choice(["minimize", "maximize"])
        lines.append(f"solve {opt} z;")
    else:
        lines.append("solve satisfy;")

    # Brute-force reference: mirror the stdlib totality guard by dropping the
    # undefined (base=0, exp<0) cells.
    ref = set()
    for xv, yv, zv in itertools.product(
            range(xlo, xhi + 1), range(ylo, yhi + 1), range(zlo, zhi + 1)):
        if fx is not None and xv != fx:
            continue
        if fy is not None and yv != fy:
            continue
        if fz is not None and zv != fz:
            continue
        want = reference_pow(xv, yv)
        if want is None:
            continue  # undefined cell, excluded by MiniZinc pow_t guard
        if want != zv:
            continue
        ref.add((xv, yv, zv))

    # Apply the int_lin_le extra constraint to the reference as well.
    m = re.search(r"constraint int_lin_le\(\[1\], \[z\], (-?\d+)\);", "\n".join(lines))
    if m:
        t = int(m.group(1))
        ref = {s for s in ref if s[2] <= t}

    text = "\n".join(lines) + "\n"
    if opt is None:
        return text, ref, None
    vals = [s[2] for s in ref]
    best = (min(vals) if opt == "minimize" else max(vals)) if vals else None
    return text, ref, (opt, best)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261027
    rng = random.Random(seed)

    ok = wrong = unknown = 0
    for i in range(n):
        text, ref, opt = gen_case(rng)
        out = run_fzn(text)
        if "=====UNKNOWN=====" in out:
            unknown += 1
            continue
        sols = parse_solutions(out)
        if "=====UNSATISFIABLE=====" in out or not sols:
            got = set()
        else:
            got = {(s["x"], s["y"], s["z"]) for s in sols if
                   all(k in s for k in ("x", "y", "z"))}

        if opt is None:
            # For satisfy, -a must enumerate EXACTLY the reference set.
            good = (got == ref)
        else:
            # For optimize, check the printed optimum and that every printed
            # solution is a valid lattice point.
            obj = re.search(r"objective=(-?\d+)", out)
            if opt[1] is None:
                good = ("=====UNSATISFIABLE=====" in out or not got)
            elif obj:
                good = abs(float(obj.group(1)) - opt[1]) < 1e-6 and got <= ref
            else:
                objs = [s["z"] for s in sols if "z" in s]
                want = min(objs) if opt[0] == "minimize" else max(objs)
                good = objs and want == opt[1] and got <= ref

        if good:
            ok += 1
        else:
            wrong += 1
            if wrong <= 6:
                print("MISMATCH case:")
                print(text)
                print("ref:", sorted(ref)[:12], "got:", sorted(got)[:12], "opt:", opt)
                print("out:", out[:300])
    print(f"pow_var_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown} (N={n}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
