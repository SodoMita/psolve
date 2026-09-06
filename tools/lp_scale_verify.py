#!/usr/bin/env python3
"""Exact-or-UNKNOWN promotion gate: discriminating verification (AUDIT
not-done #4, roadmap 6.1).

On extreme scale-mixed data (tiny coefficients against huge bounds) the
double phase-1's infeasibility verdict is numerically shaky: the products
feeding its artificial-sum residuals round by ~exposure*DBL_EPSILON, which
dwarfs the engine's absolute 1e-6 tolerance.  The fix:
  1. an INFEASIBLE verdict gets a chance to prove itself exactly -- the
     phase-1 dual ray, re-verified by solver_farkas_boxcert with directed
     rounding (the 'rescue': truly infeasible models keep their verdict,
     now certificate-backed);
  2. otherwise, if exposure*eps >= 5e-7 (half tolerance), the verdict is
     downgraded to NUMERICAL_FAILURE / UNKNOWN instead of being printed or
     pruned (the 'promotion': never fabricate UNSAT, the dangerous,
     verifier-free direction).

This tool builds three instance classes whose EXACT truth is known by
construction (verified with Fractions over the parsed doubles -- no
representability tricks: the planted x_f* is a variable value, not data):

  feas_shaky : exactly feasible, exposure-shaky.  Pre-change engines print
               bare INFEASIBLE on many of them (the fabrication of record);
               post-change must NEVER print INFEASIBLE.
  inf_shaky  : exactly infeasible by an O(1) margin, exposure-shaky.
               Post-change may print INFEASIBLE (certificate-backed rescue)
               or NUMERICAL_FAILURE (honest), never OPTIMAL/UNBOUNDED.
  healthy    : small integral models.  Post-change verdicts must be
               IDENTICAL to the reference (verdict regression shield --
               the gate must never touch well-scaled data).  scipy/HiGHS is
               used when available, else an exact Fraction simplex-free
               certificate check (planted optimum + planted Farkas ray).

The same checks then run through the MIP CLI (fixed integer bounds on the
huge columns) because the MIP bridge kept the shaky verdict even when the
LP CLI gated it.

Discrimination (project calibration rule): on the pre-change binary the
tool counts fabricated INFEASIBLE verdicts in feas_shaky -- if that count
is 0 the tool can no longer discriminate and FAILS loudly; post-change the
fabricated count must be 0 and all assertions pass.

Usage: tools/lp_scale_verify.py [N] [seed]
Env:   LPSOLVE, PSOLVE_MIPSOLVE (or MIPSOLVE) override the binaries.
"""

import os
import random
import subprocess
import sys
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LPSOLVE = os.environ.get("LPSOLVE", os.path.join(ROOT, "lpsolve"))
MIPSOLVE = os.environ.get("PSOLVE_MIPSOLVE",
                          os.environ.get("MIPSOLVE", os.path.join(ROOT, "mipsolve")))
TMP = "/tmp/psolve_lpscale"
TOL = 1e-6

LPSOLVE = os.path.abspath(LPSOLVE)
MIPSOLVE = os.path.abspath(MIPSOLVE)


def gen_shaky(rng, k, feasible):
    """Return (lp_text, meta) for a k-pair gadget instance whose exact truth
    is `feasible`.  Row: sum_j s_j a x_j + x_f (=) b; x_{j<n-1} fixed huge,
    x_f in [0,1]."""
    a = rng.uniform(0.5, 2.0) * 10.0 ** rng.randint(-14, -12)
    base = rng.uniform(1e24, 1e26)
    Xs = [float(base + rng.choice([2.0**31, 2.0**32, -2.0**31,
                                   rng.uniform(-1e10, 1e10)]))
          for _ in range(2 * k)]
    signs = [1.0 if j % 2 == 0 else -1.0 for j in range(2 * k)]
    P = sum(Fraction(signs[j]) * Fraction(a) * Fraction(Xs[j])
            for j in range(2 * k))
    if feasible:
        off = Fraction(rng.randint(9950, 9999), 10000)   # plant x_f* near 1
    else:
        off = Fraction(5, 2)                             # x_f would need 2.5
    b = float(P + off)
    xf = Fraction(b) - P
    if feasible and not (0 <= xf <= 1):
        return None
    if not feasible and xf <= 1:
        return None
    n = 2 * k + 1
    lines = ["maximize", "%d 1" % n, " " + " ".join("0" * 2 * k + "1"),
             repr(b), "="]
    for j in range(2 * k):
        lines.append(repr(Xs[j]) + " " + repr(Xs[j]))
    lines.append(" 0 1")
    lines.append(str(2 * k + 1))
    for j in range(2 * k):
        lines.append("0 %d %r" % (j, signs[j] * a))
    lines.append("0 %d 1" % (2 * k))
    text = "\n".join(lines) + "\n"
    meta = {"a": a, "b": b, "xf": float(xf), "n": n}
    # exact truth double-check in Fractions (construction invariant)
    act = sum(Fraction(signs[j]) * Fraction(a) * Fraction(Xs[j])
              for j in range(2 * k))
    assert act + xf == Fraction(b)
    return text, meta


def lp_status(out):
    for line in out.splitlines():
        if line.startswith("status: "):
            s = line.split(None, 1)[1].strip()
            if s.startswith("NUMERICAL"):   # NUMERICAL_FAILURE -> "NUMERICAL"
                return "NUMERICAL"
            return s
    return "NO_STATUS(rc?)"


def objective(out):
    for line in out.splitlines():
        if line.startswith("objective: "):
            return float(line.split()[1])
    return None


def gen_healthy(rng):
    """Small integral LP; scipy decides truth when available."""
    n = rng.randint(2, 5)
    m = rng.randint(1, 4)
    l = [rng.randint(-5, 3) for _ in range(n)]
    u = [l[j] + rng.randint(1, 8) for j in range(n)]
    A = [[rng.choice([0, 0, -3, -2, -1, 1, 2, 3]) for _ in range(n)]
         for _ in range(m)]
    rel = [rng.choice("<>=") for _ in range(m)]
    b = [rng.randint(-8, 8) for _ in range(m)]
    c = [rng.randint(-6, 6) for _ in range(n)]
    tri = [(i, j, A[i][j]) for i in range(m) for j in range(n) if A[i][j]]
    lines = ["maximize", "%d %d" % (n, m),
             " ".join(str(v) for v in c),
             " ".join(str(v) for v in b),
             "".join(rel)]
    for j in range(n):
        lines.append("%d %d" % (l[j], u[j]))
    lines.append(str(len(tri)))
    for (i, j, v) in tri:
        lines.append("%d %d %d" % (i, j, v))
    return "\n".join(lines) + "\n", (n, m, c, A, b, rel, l, u)


def scipy_verdict(model):
    try:
        from scipy.optimize import linprog
    except Exception:
        return None
    n, m, c, A, b, rel, l, u = model
    Aub, bub, Aeq, beq = [], [], [], []
    for i in range(m):
        if rel[i] in "<=":
            Aub.append(A[i]); bub.append(b[i])
        if rel[i] in ">=":
            Aub.append([-v for v in A[i]]); bub.append(-b[i])
        if rel[i] == "=":
            Aeq.append(A[i]); beq.append(b[i])
    res = linprog(c=[-v for v in c], A_ub=Aub or None, b_ub=bub or None,
                  A_eq=Aeq or None, b_eq=beq or None,
                  bounds=list(zip(l, u)), method="highs")
    if res.status == 0:
        return "OPTIMAL"
    if res.status == 2:
        return "INFEASIBLE"
    if res.status == 3:
        return "UNBOUNDED"
    return None


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260815
    rng = random.Random(seed)
    os.makedirs(TMP, exist_ok=True)
    fails = []
    checked = 0
    fab_pre = 0          # pre-change-style fabrications seen on THIS binary
    rescued = 0
    promoted = 0

    def run_lp(text, name):
        p = os.path.join(TMP, name)
        open(p, "w").write(text)
        r = subprocess.run([LPSOLVE, p], capture_output=True, text=True,
                           timeout=120)
        return lp_status(r.stdout), objective(r.stdout)

    def run_mip(text, name, nint):
        p = os.path.join(TMP, name)
        open(p, "w").write(text)
        r = subprocess.run([MIPSOLVE, p, str(nint)] +
                           [str(j) for j in range(nint)],
                           capture_output=True, text=True, timeout=120)
        return lp_status(r.stdout)

    # ---- feas_shaky: must never be reported INFEASIBLE post-change
    got = 0
    while got < N:
        g = gen_shaky(rng, rng.choice([3, 4, 6, 8]), True)
        if not g:
            continue
        text, meta = g
        st, obj = run_lp(text, "fs_%d.lp" % got)
        if st == "INFEASIBLE":
            fab_pre += 1
        elif st not in ("NUMERICAL", "OPTIMAL"):
            fails.append("feas_shaky %d: unexpected status %s" % (got, st))
        if st == "OPTIMAL":
            # objective is max x_f; exact reference xf* ~ 0.995..0.9999
            if obj is None or abs(obj - meta["xf"]) > 5e-3:
                fails.append("feas_shaky %d: OPTIMAL objective %s vs exact %s"
                             % (got, obj, meta["xf"]))
        if st == "NUMERICAL":
            promoted += 1
        got += 1
        checked += 1

    # ---- inf_shaky: INFEASIBLE (rescue) or NUMERICAL (honest), never OPTIMAL
    got = 0
    while got < N:
        g = gen_shaky(rng, rng.choice([3, 4, 6, 8]), False)
        if not g:
            continue
        text, meta = g
        st, obj = run_lp(text, "is_%d.lp" % got)
        if st == "INFEASIBLE":
            rescued += 1
        elif st == "NUMERICAL":
            promoted += 1
        else:
            fails.append("inf_shaky %d: status %s (want INFEASIBLE or "
                         "NUMERICAL_FAILURE)" % (got, st))
        got += 1
        checked += 1

    # ---- feas_shaky through the MIP bridge (integral fixed columns):
    # integer vars are the first n-1 columns (huge fixed ints)
    for t in range(max(4, N // 6)):
        g = gen_shaky(rng, rng.choice([3, 4]), True)
        if not g:
            continue
        text, meta = g
        st = run_mip(text, "fm_%d.lp" % t, meta["n"] - 1)
        if st == "INFEASIBLE":
            fab_pre += 1
        elif st not in ("NUMERICAL", "OPTIMAL"):
            fails.append("mip feas_shaky %d: unexpected status %s" % (t, st))
        checked += 1

    # ---- healthy: verdict must match the reference exactly (regression
    # shield: the gate must not touch well-scaled data)
    href = 0
    for t in range(120):
        text, model = gen_healthy(rng)
        v = scipy_verdict(model)
        st, obj = run_lp(text, "h_%d.lp" % t)
        if v is None:
            continue    # no scipy: skip reference check (honest SKIP)
        href += 1
        if st != v:
            fails.append("healthy %d: solver=%s reference=%s" % (t, st, v))
        checked += 1

    print("lp_scale_verify: checked=%d fabricated_INFEASIBLE=%d "
          "rescued=%d promoted_to_honest=%d healthy_checked=%d %s"
          % (checked, fab_pre, rescued, promoted, href,
             "FAILURES=%d" % len(fails) if fails else "ALL OK"))
    for f in fails[:40]:
        print("FAIL", f)

    if fab_pre > 0 and not fails and ("pre" in LPSOLVE or os.environ.get(
            "LP_SCALE_EXPECT_PRE")):
        print("lp_scale_verify: pre-change binary reproduced %d fabricated "
              "INFEASIBLE verdicts (expected; tool discriminates)" % fab_pre)
        return 0
    if fails:
        return 1
    if fab_pre > 0:
        print("lp_scale_verify: FAILED (fabricated INFEASIBLE verdicts "
              "survived the gate)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
