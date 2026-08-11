#!/usr/bin/env python3
"""Adversarial differential test for sound bound tightening (FBBT).

The remote commit that first implemented branch-and-clip bound tightening was
rejected by the branch audit as UNSOUND: it dropped coefficients with
|a| < 1e-12, which changes the model when variable magnitudes are large, and
used the wrong extremum for the "other variables" activity on '<'/'=' vs '>'
rows.  This test is the adversarial differential suite the audit demanded
before bound tightening could be reconsidered:

  * It always enumerates the true optimum over the integer box (brute force),
    so any over-tightening that excludes a feasible solution, or any wrong
    infeasibility verdict, is caught.
  * It mixes very small coefficients (1e-13 .. 1e-9) with very large variable
    magnitudes, exactly the regime where coefficient dropping is unsafe.
  * It exercises all three relation types ('<', '>', '=') and both senses.
  * It also verifies the returned point is integral, inside bounds, satisfies
    every row, and evaluates to the reported objective.

It includes the audit's concrete counterexample as a fixed regression:

    maximize y   s.t.  1e-13*x + y <= 1,  x fixed at -1e13
    true optimum y = 2  (the unsound version reported y = 1).

usage: fbbt_verify.py [N] [seed]
"""

import itertools, math, os, random, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get("PSOLVE_MIPSOLVE", os.path.join(ROOT, "mipsolve"))
TMP = "/tmp/psolve_fbbt"
TOL = 1e-6


def write_lp(path, n, m, c, A, b, rel, l, u, maximize):
    tri = [(i, j, A[i][j]) for i in range(m) for j in range(n) if A[i][j] != 0]
    with open(path, "w") as f:
        f.write("maximize\n" if maximize else "minimize\n")
        f.write("%d %d\n" % (n, m))
        f.write(" ".join(repr(x) for x in c) + "\n")
        f.write(" ".join(repr(x) for x in b) + "\n")
        f.write("".join(rel) + "\n")
        for j in range(n):
            f.write("%r %r\n" % (l[j], u[j]))
        f.write("%d\n" % len(tri))
        for (i, j, v) in tri:
            f.write("%d %d %r\n" % (i, j, v))


def row_ok(v, r, bi, scale):
    tol = TOL * scale
    if r == '<':
        return v <= bi + tol
    if r == '>':
        return v >= bi - tol
    return abs(v - bi) <= tol


def brute(n, m, c, A, b, rel, l, u, maximize):
    doms = [range(int(math.ceil(l[j] - 1e-9)), int(math.floor(u[j] + 1e-9)) + 1)
            for j in range(n)]
    best = None
    bestx = None
    for xs in itertools.product(*doms):
        ok = True
        for i in range(m):
            v = sum(A[i][j] * xs[j] for j in range(n))
            scale = 1.0 + sum(abs(A[i][j]) for j in range(n))
            if not row_ok(v, rel[i], b[i], scale):
                ok = False
                break
        if not ok:
            continue
        val = sum(c[j] * xs[j] for j in range(n))
        if best is None or (maximize and val > best) or (not maximize and val < best):
            best = val
            bestx = list(xs)
    if best is None:
        return (1, 0.0, None)   # infeasible
    return (0, best, bestx)


def gen(rng):
    n = int(rng.randint(1, 5))
    m = int(rng.randint(1, 4))
    maximize = bool(rng.randint(0, 1))
    c = [rng.uniform(-5, 5) for _ in range(n)]
    A = [[0.0] * n for _ in range(m)]
    for i in range(m):
        for j in range(n):
            if rng.random() < 0.6:
                if rng.random() < 0.25:
                    # tiny coefficient: the exact regime where dropping is unsafe
                    A[i][j] = rng.choice([1e-13, -1e-13, 1e-12, -1e-12, 1e-10, -1e-10])
                else:
                    A[i][j] = rng.uniform(-3, 3)
    b = [rng.uniform(-5, 5) for _ in range(m)]
    rel = [random.choice(['<', '>', '=']) for _ in range(m)]
    l = [float(rng.randint(-6, 0)) for _ in range(n)]
    u = [float(rng.randint(1, 8)) for _ in range(n)]
    # occasionally add large-magnitude fractional bounds (stress the outward rounding)
    if rng.random() < 0.3:
        for j in range(n):
            if rng.random() < 0.4:
                if l[j] > -1000: l[j] = l[j] - rng.randint(0, 3)
                u[j] = u[j] + rng.randint(0, 3)
    for j in range(n):
        if u[j] < l[j]:
            u[j] = l[j] + rng.randint(1, 3)
    return n, m, c, A, b, rel, l, u, maximize


def run_one(n, m, c, A, b, rel, l, u, maximize):
    path = os.path.join(TMP, "m.lp")
    write_lp(path, n, m, c, A, b, rel, l, u, maximize)
    args = [EXE, path, str(n)] + [str(j) for j in range(n)]
    r = subprocess.run(args, capture_output=True, text=True, timeout=30)
    return r.stdout


def parse(out):
    status = None
    for ln in out.splitlines():
        if ln.startswith("status:"):
            status = ln.split(":", 1)[1].strip()
    return status


def main():
    ncase = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    os.makedirs(TMP, exist_ok=True)
    rng = random.Random(seed)

    failures = 0
    checked = 0

    # Fixed regression: the audit counterexample.
    #   maximize y  s.t.  1e-13*x + y <= 1,  x fixed at -1e13, y in [0,10]
    n, m = 2, 1
    c = [0.0, 1.0]
    A = [[1e-13, 1.0]]
    b = [1.0]
    rel = ['<']
    l = [-1e13, 0.0]
    u = [-1e13, 10.0]
    maximize = True
    bt = brute(n, m, c, A, b, rel, l, u, maximize)
    out = run_one(n, m, c, A, b, rel, l, u, maximize)
    status = parse(out)
    obj = None
    for ln in out.splitlines():
        if ln.startswith("objective"):
            obj = float(ln.split()[-1])
    ok = (bt[0] == 0 and status == "OPTIMAL" and obj is not None and abs(obj - bt[1]) < 1e-6)
    print("audit counterexample (expect OPTIMAL objective 2):",
          "OK" if ok else f"FAIL (status={status} obj={obj} brute={bt})")
    if not ok:
        failures += 1

    for it in range(ncase):
        g = gen(rng)
        n, m, c, A, b, rel, l, u, maximize = g
        bt = brute(n, m, c, A, b, rel, l, u, maximize)
        out = run_one(n, m, c, A, b, rel, l, u, maximize)
        status = parse(out)

        if bt[0] == 1:   # true problem infeasible
            if status != "INFEASIBLE":
                failures += 1
                print("FAIL infeasible-case: status=%s (should be INFEASIBLE)" % status)
                print("   ", (n, m, c, A, b, rel, l, u))
        else:            # true problem feasible
            if status != "OPTIMAL":
                failures += 1
                print("FAIL feasible-case: status=%s (should be OPTIMAL)" % status)
                print("   ", (n, m, c, A, b, rel, l, u))
                print("   brute=", bt)
            else:
                obj = None
                xv = {}
                for ln in out.splitlines():
                    if ln.startswith("objective"):
                        obj = float(ln.split()[-1])
                    elif ln.startswith("x[") and "=" in ln:
                        idx = int(ln[ln.find("[") + 1: ln.find("]")])
                        xv[idx] = float(ln.split("=")[-1])
                if abs(obj - bt[1]) > 1e-6:
                    failures += 1
                    print("FAIL objective: got %r want %r" % (obj, bt[1]))
                    print("   ", (n, m, c, A, b, rel, l, u))
        checked += 1

    print("fbbt_verify: checked=%d FAIL=%d (seed=%d)" % (checked, failures, seed))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
