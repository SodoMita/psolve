#!/usr/bin/env python3
"""Exactness / overflow regression test for the fixed-point (exact rational)
LP solver `fxsolve`.

What it enforces (the property the exact solver exists for):

  *No wrong answers.*  Every reported OPTIMAL must be exactly primal feasible
  (checked in Python `Fraction` arithmetic, not floating point), its exact
  objective must equal c.x exactly, and it must agree with an independent
  reference (the double `lpsolve`, or scipy/HiGHS when available).  Every
  reported INFEASIBLE/UNBOUNDED must agree with the reference.  OVERFLOW and
  ITERATION_LIMIT are "no answer" outcomes and are allowed (they are honest),
  they are only counted and reported.

Before the overflow-checked rewrite, exact-rational products were truncated
from __int128 back to int64 without a check, so ordinary instances (a dense
8x8 LP with 3-digit coefficients, or any LP whose data have ~16 decimals)
produced silently WRONG answers -- e.g. an infeasible LP reported as
"OPTIMAL, objective (exact) 0/1".

usage: fx_exact_test.py [N] [seed]
"""
import os, random, subprocess, sys
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FX = os.path.join(ROOT, "fxsolve")
LP = os.path.join(ROOT, "lpsolve")
TMP = "/tmp/fx_exact_test.lp"


def gen(rnd, n, m, scale, decimals=0, dens=1.0, sense=None):
    """Random LP in the .lp text format; returns (text, model)."""
    sense = sense or rnd.choice(["maximize", "minimize"])

    def num():
        v = rnd.randint(1, scale)
        if decimals:
            return Fraction(v, 10 ** decimals)
        return Fraction(v)

    c = [num() for _ in range(n)]
    rel = "".join(rnd.choice("<<<>=") for _ in range(m))
    b = [num() * rnd.randint(1, 4) for _ in range(m)]
    A = [[num() if rnd.random() < dens else Fraction(0) for _ in range(n)] for _ in range(m)]
    lo = [Fraction(0)] * n
    hi = [None] * n            # None = +inf
    for j in range(n):
        if rnd.random() < 0.3:
            hi[j] = num() * rnd.randint(1, 5)

    def fmt(x):
        return str(float(x)) if x.denominator != 1 else str(x.numerator)

    lines = [sense, f"{n} {m}", " ".join(fmt(v) for v in c), " ".join(fmt(v) for v in b), rel]
    for j in range(n):
        lines.append(f"{fmt(lo[j])} " + ("inf" if hi[j] is None else fmt(hi[j])))
    trip = [(i, j, A[i][j]) for i in range(m) for j in range(n) if A[i][j] != 0]
    lines.append(str(len(trip)))
    for i, j, v in trip:
        lines.append(f"{i} {j} {fmt(v)}")
    return "\n".join(lines) + "\n", dict(sense=sense, n=n, m=m, c=c, b=b, rel=rel, A=A, lo=lo, hi=hi)


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    out = {}
    for ln in p.stdout.splitlines():
        if ":" in ln:
            k, v = ln.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def exact_values(stdout_lines):
    """Parse `x[j] = ... (exact p/q)` lines into Fractions."""
    xs = {}
    for ln in stdout_lines:
        if ln.startswith("x[") and "(exact" in ln:
            j = int(ln.split("[")[1].split("]")[0])
            frac = ln.split("(exact")[1].strip().rstrip(")").strip()
            p, q = frac.split("/")
            xs[j] = Fraction(int(p), int(q))
    return xs


def check_exact_feasible(model, x, obj):
    n, m = model["n"], model["m"]
    for j in range(n):
        if x[j] < model["lo"][j]:
            return f"var {j} below lower bound"
        if model["hi"][j] is not None and x[j] > model["hi"][j]:
            return f"var {j} above upper bound"
    for i in range(m):
        lhs = sum(model["A"][i][j] * x[j] for j in range(n))
        r, rhs = model["rel"][i], model["b"][i]
        if r == "<" and lhs > rhs:
            return f"row {i} violates <= exactly"
        if r == ">" and lhs < rhs:
            return f"row {i} violates >= exactly"
        if r == "=" and lhs != rhs:
            return f"row {i} violates = exactly"
    got = sum(model["c"][j] * x[j] for j in range(n))
    if got != obj:
        return f"objective mismatch: reported {obj} but c.x = {got}"
    return None


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    rnd = random.Random(seed)

    wrong = 0

    # CSC triplets are allowed to repeat a (row, column) pair and therefore
    # must be summed.  The old dense exact reader overwrote the first entry,
    # solving x <= 2 instead of x + x <= 2 and confidently returning 2.
    duplicate_lp = """maximize
1 1
1
2
<
0 10
2
0 0 1
0 0 1
"""
    with open(TMP, "w") as f:
        f.write(duplicate_lp)
    dup = run([FX, TMP, "--print"])
    if dup.get("status") != "OPTIMAL" or dup.get("objective (exact)") != "1/1":
        wrong += 1
        print("  WRONG duplicate-triplet regression:", dup)

    stats = {"OPTIMAL": 0, "INFEASIBLE": 0, "UNBOUNDED": 0, "OVERFLOW": 0,
             "ITERATION_LIMIT": 0, "OTHER": 0}
    widths = {64: 0, 128: 0}
    wide_disagree = 0

    for t in range(N):
        n = rnd.randint(2, 9)
        m = rnd.randint(1, 8)
        scale = rnd.choice([3, 10, 100, 1000, 100000])
        decimals = rnd.choice([0, 0, 0, 1, 2])
        text, model = gen(rnd, n, m, scale, decimals, dens=rnd.choice([0.5, 0.8, 1.0]))
        with open(TMP, "w") as f:
            f.write(text)

        p = subprocess.run([FX, TMP, "--print"], capture_output=True, text=True, timeout=120)
        lines = p.stdout.splitlines()
        info = {}
        for ln in lines:
            if ":" in ln:
                k, v = ln.split(":", 1)
                info[k.strip()] = v.strip()
        st = info.get("status", "OTHER")
        stats[st if st in stats else "OTHER"] += 1
        w = 128 if "128" in info.get("width", "") else 64
        widths[w] += 1

        ref = run([LP, TMP])
        rst = ref.get("status")

        if st == "OPTIMAL":
            xs = exact_values(lines)
            objs = info.get("objective (exact)", "0/1").split("/")
            obj = Fraction(int(objs[0]), int(objs[1]))
            err = check_exact_feasible(model, xs, obj)
            if err:
                wrong += 1
                print(f"  WRONG t={t}: {err}")
                continue
            # cross-check the optimum against the double solver
            if rst == "OPTIMAL":
                d = float(ref["objective"])
                if abs(d - float(obj)) > 1e-6 * max(1.0, abs(d)):
                    wrong += 1
                    print(f"  WRONG t={t}: optimum {float(obj)} vs double {d}")
                    continue
            elif rst in ("INFEASIBLE",):
                wrong += 1
                print(f"  WRONG t={t}: fx OPTIMAL but double INFEASIBLE")
                continue
        elif st == "INFEASIBLE" and rst == "OPTIMAL":
            wrong += 1
            print(f"  WRONG t={t}: fx INFEASIBLE but double found an optimum")
            continue
        elif st == "UNBOUNDED" and rst == "OPTIMAL":
            wrong += 1
            print(f"  WRONG t={t}: fx UNBOUNDED but double found a finite optimum")
            continue

        # the wide (128-bit) core must never contradict the fast core
        if st in ("OPTIMAL", "INFEASIBLE", "UNBOUNDED") and w == 64:
            pw = subprocess.run([FX, TMP, "--wide"], capture_output=True, text=True, timeout=120)
            winfo = {}
            for ln in pw.stdout.splitlines():
                if ":" in ln:
                    k, v = ln.split(":", 1)
                    winfo[k.strip()] = v.strip()
            wst = winfo.get("status")
            if wst != st or (st == "OPTIMAL" and
                             winfo.get("objective (exact)") != info.get("objective (exact)")):
                wide_disagree += 1
                wrong += 1
                print(f"  WRONG t={t}: 64-bit says {st}/{info.get('objective (exact)')} "
                      f"but 128-bit says {wst}/{winfo.get('objective (exact)')}")

    print(f"fx_exact_test: WRONG={wrong} wide_disagree={wide_disagree} "
          f"(N={N}, seed={seed})")
    print(f"  statuses: {stats}")
    print(f"  widths:   64-bit={widths[64]} 128-bit={widths[128]}")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
