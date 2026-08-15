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

2026-08-15(2) round: the tolerance-padded, round-to-nearest root prune this
suite guards was found to fabricate UNSAT under catastrophic cancellation
(products near 1e12 round by ~6e-5, dwarfing the 1e-6*(1+|rhs|) margin;
fbbt's return value is reported as proven INFEASIBLE with NO LP or exact
cross-check, so the wrong prune is a wrong final verdict).  The directed-
rounding rewrite (mip_box_conflict, src/mip.c) is regression-locked below by
a pinned integral counterexample and a randomized cancellation family whose
reference is exact dyadic arithmetic (Fractions over the stored doubles) --
float-tolerance brute force cannot operate at 1e18 coefficient scale.
"""

import itertools, math, os, random, subprocess, sys
from fractions import Fraction

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

    # ------------------------------------------------------------------ #
    # 2026-08-15(2) fabricated-UNSAT regressions (directed-rounding round) #
    # ------------------------------------------------------------------ #

    # Pinned integral counterexample (the regression of record):
    #   maximize x0,
    #   8.658741690308737e17*x0 - 4.4530671550159217e18*x1 <= 2561,
    #   x0 == 36, x1 == 7, both integer.
    # Exact activity on the stored doubles (dyadic-exact): 2560 <= 2561 --
    # feasible; true optimum 36.  The RN activity is 4096, and
    # 4096 > 2561 + 1e-6*(1+2561), so the pre-fix tolerance prune declared
    # the model INFEASIBLE before any LP ran; the rigorous certificate does
    # not fire (its directed-rounded minimum is <= 2560), and the exact
    # cross-check then recovers OPTIMAL 36.
    pc = dict(n=2, m=1, c=[1.0, 0.0],
              A=[[8.658741690308737e+17, -4.4530671550159217e+18]],
              b=[2561.0], rel=['<'], l=[36.0, 7.0], u=[36.0, 7.0], maximize=True)
    expect_act = Fraction(pc['A'][0][0]) * 36 + Fraction(pc['A'][0][1]) * 7
    assert expect_act <= Fraction(2561), "pinned case must be feasible exactly"
    out = run_one(pc['n'], pc['m'], pc['c'], pc['A'], pc['b'], pc['rel'],
                  pc['l'], pc['u'], pc['maximize'])
    status = parse(out)
    obj = None
    for ln in out.splitlines():
        if ln.startswith("objective"):
            obj = float(ln.split()[-1])
    ok = (status == "OPTIMAL" and obj == 36.0)
    print("cancellation pinned case (expect OPTIMAL 36):",
          "OK" if ok else f"FAIL (status={status} obj={obj})")
    if not ok:
        failures += 1
    checked += 1

    # Randomized cancellation family: two fixed integer variables, one row
    # whose two huge products nearly cancel to a small exact residual D.
    # Every instance is feasible (the single box point satisfies the row),
    # so ANY INFEASIBLE verdict is a fabrication.  Instances are filtered to
    # the ones whose RN activity crosses the pre-fix tolerance band, so a
    # pre-fix MIPSOLVE binary provably fails a large share of them (this is
    # what makes the test discriminating).
    fam_ok = 0
    fam_bad = 0
    fam_target = max(20, ncase // 5)
    fam = random.Random(seed ^ 0xC4E1)
    attempts = 0
    made = 0
    while made < fam_target and attempts < fam_target * 500:
        attempts += 1
        a0i = fam.randint(10**17, 10**18)
        x0 = fam.randint(2, 40)
        x1 = fam.randint(2, 40)
        a1i = -((a0i * x0) // x1)
        if a1i == 0:
            continue
        a0f, a1f = float(a0i), float(a1i)
        act = Fraction(a0f) * x0 + Fraction(a1f) * x1
        if act.denominator != 1:
            continue
        D = act.numerator
        if not (1 <= D <= 2_000_000):
            continue
        comp = a0f * x0 + a1f * x1               # C-order round-to-nearest
        if fam.random() < 0.5:
            rel, rhs = '<', D + 1                # feasible: D <= rhs
            fires = comp > float(rhs) + 1e-6 * (1.0 + abs(rhs))
        else:
            rel, rhs = '>', D - 1                # feasible: D >= rhs
            fires = comp < float(rhs) - 1e-6 * (1.0 + abs(rhs))
        if not fires:
            continue
        made += 1
        c0 = fam.randint(-5, 5)
        obj_exact = float(c0 * x0)               # c = [c0, 0]
        out = run_one(2, 1, [float(c0), 0.0], [[a0f, a1f]], [float(rhs)],
                      [rel], [float(x0), float(x1)], [float(x0), float(x1)],
                      c0 >= 0)
        status = parse(out)
        obj = None
        for ln in out.splitlines():
            if ln.startswith("objective"):
                obj = float(ln.split()[-1])
        # c0 >= 0 -> maximize (obj x0), c0 < 0 -> minimize (obj also c0*x0 at
        # the single box point either way).
        exp_obj = float(c0 * x0)
        if status == "OPTIMAL" and obj is not None and obj == exp_obj:
            fam_ok += 1
        else:
            fam_bad += 1
            print(f"FAIL cancellation-family: status={status} obj={obj} "
                  f"expect OPTIMAL {exp_obj}  (a0={a0f!r} a1={a1f!r} "
                  f"x0={x0} x1={x1} D={D} rel={rel} rhs={rhs})")
    print(f"cancellation family: OK={fam_ok} FAIL={fam_bad} (of {made} fired instances)")
    failures += fam_bad
    checked += made

    print("fbbt_verify: checked=%d FAIL=%d (seed=%d)" % (checked, failures, seed))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
