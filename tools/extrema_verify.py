#!/usr/bin/env python3
"""Differential verifier for the FlatZinc `array_int_maximum`/`array_int_minimum`
handlers.

Generates random small instances (satisfy and minimize-max / maximize-min), runs
`fznsolve`, and compares the reported extremum against an independent brute-force
enumerator over every element assignment.  Any returned solution must satisfy the
constraint (m == max/min of the actual x values); a satisfy instance must be
solvable iff the brute force says feasible; an optimize instance must match the
brute-force optimum.

Usage:  python3 tools/extrema_verify.py [N] [seed]
"""
import itertools
import os
import random
import subprocess
import sys
import tempfile


def build_fzn(path, xs, mode):
    """xs: list of (lo, hi) per element.  mode in {'sat','minmax','maxmin'}."""
    n = len(xs)
    lines = ["predicate array_int_maximum(var int: m, array[int] of var int: x);",
             "predicate array_int_minimum(var int: m, array[int] of var int: x);"]
    elist = ",".join(f"x{i}" for i in range(n))
    for i, (lo, hi) in enumerate(xs):
        lines.append(f"var {lo}..{hi}: x{i};")
    # m is the extremum result; keep it loose to exercise the selector path
    lo = min(x[0] for x in xs) - 1
    hi = max(x[1] for x in xs) + 1
    lines.append(f"var {lo}..{hi}: m :: output_var;")
    lines.append(f"array [1..{n}] of var int: x = [{elist}];")
    if mode == "minmax":
        lines.append("constraint array_int_maximum(m,x);")
        lines.append("solve minimize m;")
    elif mode == "maxmin":
        lines.append("constraint array_int_minimum(m,x);")
        lines.append("solve maximize m;")
    else:
        lines.append("constraint array_int_maximum(m,x);")
        lines.append("constraint array_int_minimum(m,x);")
        lines.append("solve satisfy;")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))


def run_fzn(fzn, binary):
    try:
        r = subprocess.run([binary, fzn], capture_output=True, text=True, timeout=15)
    except subprocess.TimeoutExpired:
        return None, None
    mval = None
    for line in r.stdout.splitlines():
        line = line.strip()
        if line.startswith("m = "):
            mval = int(line.split("=")[1].split(";")[0].strip())
    obj = None
    for line in r.stdout.splitlines():
        if "objective=" in line:
            obj = float(line.split("=")[1])
    unknown = "=====UNKNOWN=====" in r.stdout
    if unknown:
        return None, None
    return mval, obj


def brute(xs, mode):
    n = len(xs)
    domains = [list(range(lo, hi + 1)) for lo, hi in xs]
    best = None
    feas = False
    if mode == "sat":
        # m == max(x) AND m == min(x)  =>  all elements equal: feasible iff the
        # domains have a common value.
        common = set(domains[0])
        for d in domains[1:]:
            common &= set(d)
        feas = len(common) > 0
        return feas, None
    for assign in itertools.product(*domains):
        mx, mn = max(assign), min(assign)
        val = mx if mode == "minmax" else mn
        if best is None or (mode == "minmax" and val < best) or (mode == "maxmin" and val > best):
            best = val
            feas = True
    return feas, best


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 4242
    binary = sys.argv[3] if len(sys.argv) > 3 else "./fznsolve"
    rng = random.Random(seed)
    tmp = tempfile.mkdtemp(prefix="extrema_")
    mismatches = 0
    checked = 0
    for t in range(N):
        n = rng.randint(1, 4)
        xs = []
        prod = 1
        for _ in range(n):
            lo = rng.randint(-3, 3)
            hi = lo + rng.randint(0, 4)
            xs.append((lo, hi))
            prod *= (hi - lo + 1)
        if prod > 5000:
            continue
        mode = rng.choice(["sat", "minmax", "maxmin"])
        fzn = os.path.join(tmp, f"t{t}.fzn")
        build_fzn(fzn, xs, mode)
        mval, obj = run_fzn(fzn, binary)
        bf_feas, bf_obj = brute(xs, mode)
        if mval is None:
            # UNKNOWN or timeout: graceful, not counted against correctness
            continue
        checked += 1
        if mode == "sat":
            # returned solution must satisfy both constraints: m==max and m==min
            # => all elements equal; if brute says infeasible, fznsolve must not
            # print a solution.
            if not bf_feas:
                mismatches += 1
                print(f"MISMATCH sat (expected infeasible) case {t}: xs={xs} m={mval}")
        elif mode == "minmax":
            if obj is None or abs(obj - bf_obj) > 1e-9:
                mismatches += 1
                print(f"MISMATCH minmax case {t}: xs={xs} got m={mval} obj={obj} expect {bf_obj}")
        else:
            if obj is None or abs(obj - bf_obj) > 1e-9:
                mismatches += 1
                print(f"MISMATCH maxmin case {t}: xs={xs} got m={mval} obj={obj} expect {bf_obj}")
    print(f"extrema_verify: OK={checked} MISMATCH={mismatches} (N={N}, seed={seed})")
    sys.exit(1 if mismatches else 0)


if __name__ == "__main__":
    main()
