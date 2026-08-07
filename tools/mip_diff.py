#!/usr/bin/env python3
"""MIP status + solution differential test against exhaustive enumeration.

tools/mip_verify.py only checks the objective of runs that come back OPTIMAL --
it `continue`s on every other status.  That leaves the statuses that matter most
for honesty untested: a MIP that is really feasible must never be reported
INFEASIBLE, and a reported optimum must really be optimal, integral and
feasible.  (The LP layer underneath had exactly this class of bug.)

This test generates small *fully bounded, all-integer* problems, so the truth is
computable by enumerating the whole lattice, and then checks:

Set PSOLVE_MIPSOLVE to point the test at a different mipsolve binary (used to
confirm a fix actually changes the outcome).

  * status agreement          -- OPTIMAL vs INFEASIBLE (bounded => never
                                 UNBOUNDED), the two are never swapped;
  * objective agreement       -- reported objective == brute-force optimum;
  * solution validity         -- the printed x is integral, inside its bounds,
                                 satisfies every row, and evaluates to the
                                 reported objective.

A run is only counted when the solver reaches a definitive status; node/
iteration limits are reported separately (they are honest, not wrong).

usage: mip_diff.py [N] [seed]
"""
import itertools, math, os, random, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/psolve_mipdiff"
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
    """Exact optimum over the integer box; returns (status, obj, x)."""
    best, bestx = None, None
    lat = [range(int(math.ceil(l[j] - 1e-9)), int(math.floor(u[j] + 1e-9)) + 1)
           for j in range(n)]
    for combo in itertools.product(*lat):
        ok = True
        for i in range(m):
            v = sum(A[i][j] * combo[j] for j in range(n))
            scale = 1.0 + abs(b[i]) + max([abs(A[i][j] * combo[j]) for j in range(n)] + [0.0])
            if not row_ok(v, rel[i], b[i], scale):
                ok = False
                break
        if not ok:
            continue
        obj = sum(c[j] * combo[j] for j in range(n))
        if best is None or (obj > best + 0.0 if maximize else obj < best):
            best, bestx = obj, list(combo)
    if best is None:
        return "INFEASIBLE", None, None
    return "OPTIMAL", best, bestx


def run(path, n):
    idx = [str(j) for j in range(n)]
    exe = os.environ.get("PSOLVE_MIPSOLVE", os.path.join(ROOT, "mipsolve"))
    r = subprocess.run([exe, path, str(n)] + idx + ["--print"],
                       capture_output=True, text=True, timeout=60, cwd=ROOT)
    status, obj, x = None, None, [None] * n
    for line in r.stdout.splitlines():
        if line.startswith("status: "):
            status = line.split(None, 1)[1].strip()
        elif line.startswith("objective: "):
            obj = float(line.split()[1])
        elif line.startswith("x["):
            j = int(line[2:line.index("]")])
            x[j] = float(line.split("=", 1)[1])
    if r.returncode != 0:
        status = "CRASH(rc=%d)" % r.returncode
    return status, obj, x


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 12345
    rng = random.Random(seed)
    os.makedirs(TMP, exist_ok=True)
    path = os.path.join(TMP, "case.lp")

    bad = []
    counts = {}
    tested = 0
    for it in range(N):
        n = rng.randint(1, 4)
        m = rng.randint(1, 4)
        maximize = rng.random() < 0.5
        c = [round(rng.uniform(-5, 5), 3) for _ in range(n)]
        A = [[round(rng.uniform(-3, 3), 3) if rng.random() < 0.7 else 0.0
              for _ in range(n)] for _ in range(m)]
        rel = [rng.choice(['<', '<', '>', '=']) for _ in range(m)]
        l = [float(rng.randint(-3, 0)) for _ in range(n)]
        u = [l[j] + float(rng.randint(0, 5)) for j in range(n)]
        # Fractional bounds on an integer variable are legal and were a real
        # trap: rounding a relaxation value and clamping it to u = 1.875 used
        # to yield a "integer" solution of 1.875.
        for j in range(n):
            if rng.random() < 0.35:
                u[j] = round(u[j] + rng.uniform(0.05, 0.95), 3)
            if rng.random() < 0.2:
                l[j] = round(l[j] - rng.uniform(0.05, 0.95), 3)
            if math.floor(u[j] + 1e-9) < math.ceil(l[j] - 1e-9):
                l[j], u[j] = float(int(l[j])), float(int(l[j]))   # keep non-empty
        if it % 2 == 0:
            # Feasible by construction: plant an integer point and put every
            # right-hand side on the satisfied side of it.  Without this, a
            # random b makes ~3 out of 4 instances infeasible and the
            # optimality logic barely gets exercised.
            x0 = [float(rng.randint(int(math.ceil(l[j] - 1e-9)),
                                    int(math.floor(u[j] + 1e-9)))) for j in range(n)]
            b = []
            for i in range(m):
                v = sum(A[i][j] * x0[j] for j in range(n))
                if rel[i] == '<':
                    b.append(round(v + abs(rng.gauss(0, 1)), 3))
                elif rel[i] == '>':
                    b.append(round(v - abs(rng.gauss(0, 1)), 3))
                else:
                    b.append(round(v, 6))
        else:
            # Unstructured: a tight/equality-heavy mix, mostly infeasible, which
            # is what catches a false OPTIMAL on an infeasible model.
            b = [round(rng.uniform(-4, 8), 3) for _ in range(m)]

        write_lp(path, n, m, c, A, b, rel, l, u, maximize)
        try:
            status, obj, x = run(path, n)
        except subprocess.TimeoutExpired:
            bad.append((it, "TIMEOUT", "", ""))
            continue
        counts[status] = counts.get(status, 0) + 1

        truth, tobj, tx = brute(n, m, c, A, b, rel, l, u, maximize)
        if status in ("NODE_LIMIT", "FEASIBLE_LIMIT", "STOPPED", "NUMERICAL_FAILURE"):
            continue                      # honest non-answers
        tested += 1
        if status != truth:
            bad.append((it, "STATUS %s != %s" % (status, truth),
                        "obj=%s" % obj, "true=%s" % tobj))
            continue
        if status != "OPTIMAL":
            continue
        if abs(obj - tobj) > 1e-4 * (1 + abs(tobj)):
            bad.append((it, "OBJ", "got %.10g" % obj, "true %.10g" % tobj))
            continue
        # the printed point must itself be valid and match the objective
        why = None
        for j in range(n):
            if x[j] is None:
                why = "no x[%d]" % j
            elif abs(x[j] - round(x[j])) > 1e-6:
                why = "x[%d]=%g not integral" % (j, x[j])
            elif x[j] < l[j] - TOL or x[j] > u[j] + TOL:
                why = "x[%d]=%g out of [%g,%g]" % (j, x[j], l[j], u[j])
        if why is None:
            for i in range(m):
                v = sum(A[i][j] * x[j] for j in range(n))
                scale = 1.0 + abs(b[i]) + max([abs(A[i][j] * x[j]) for j in range(n)] + [0.0])
                if not row_ok(v, rel[i], b[i], scale):
                    why = "row %d: %g %s %g" % (i, v, rel[i], b[i])
                    break
        if why is None:
            xobj = sum(c[j] * x[j] for j in range(n))
            if abs(xobj - obj) > 1e-6 * (1 + abs(obj)):
                why = "objective %g != c.x %g" % (obj, xobj)
        if why is not None:
            bad.append((it, "SOLUTION", why, ""))

    print("mip_diff: checked=%d WRONG=%d  (N=%d, seed=%d)" % (tested, len(bad), N, seed))
    print("  statuses: " + ", ".join("%s=%d" % kv for kv in sorted(counts.items())))
    for b_ in bad[:15]:
        print("  it=%d %s %s %s" % b_)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
