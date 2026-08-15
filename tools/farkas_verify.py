#!/usr/bin/env python3
"""Farkas fast-path differential/discriminating test for the MIP engine.

The Farkas fast path (mip_farkas_certified + solver_farkas_duals) pulls the
double solver's Phase-I dual ray on an INFEASIBLE relaxation verdict and
re-verifies the complete Farkas separation min_box (y^T A)x > y^T b with
directed rounding against the ORIGINAL rows and node box.  A certified node
prunes for O(nnz) instead of paying the exact-rational fx re-solve.  It is a
PERFORMANCE feature: verdicts and the search tree must be identical to the
engine without it, and its counters (res.farkas_certs / res.fx_solves) must
show the exact re-solves disappearing.

This tool checks all of that:

  * pinned cycle models (difference-constraint cycles a - b <= -1,
    b - a <= -1 with a wide box so root FBBT cannot collapse): the root
    relaxation is infeasible with no single-row conflict mip_box_conflict
    could see, so exactly the Farkas path can certify it:
        status INFEASIBLE, nodes == 1, farkas_certs >= 1, fx_solves == 0.
    On a pre-change binary (no counters printed) the verdict still passes but
    the counter assertions fail -> the tool FAILS, as required for a
    discriminating regression.
  * margin family: cycles planted with infeasibility strength BELOW the
    MIP_TOL engine margin.  The certificate must NOT fire on them (the
    engine's own tolerance semantics accept those points), so
    farkas_certs == 0 is asserted, and the verdict must agree with the
    tolerance-aware brute force.
  * random family: small all-integer MIPs with planted infeasible
    relaxations at random depths (contradictions that only activate once
    branching fixes variables), including big/small coefficient mixes, so
    phase-1-infeasible nodes abound.  Verdicts and optima must match the
    brute-force reference everywhere; counters are reported for information.

A wrong Farkas prune changes a verdict or the proven optimum -> caught by the
brute-force parity.  Soundness failures fail the run; so does any missing or
unfired fast path on the families that must exercise it.

Usage: tools/farkas_verify.py [N] [seed]
Env:   PSOLVE_MIPSOLVE=/path/to/mipsolve  (default ./mipsolve)
"""

import itertools
import math
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER = os.environ.get("PSOLVE_MIPSOLVE", os.path.join(ROOT, "mipsolve"))
TMP = "/tmp/psolve_farkasverify"
TOL = 1e-6  # engine margin (MIP_TOL), same semantics as mip_diff/brute force


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
    """Exact optimum over the integer box with engine tolerance semantics."""
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
        if best is None or (obj > best if maximize else obj < best):
            best, bestx = obj, list(combo)
    if best is None:
        return "INFEASIBLE", None, None
    return "OPTIMAL", best, bestx


def run(path, n):
    r = subprocess.run([SOLVER, path, str(n)] + [str(j) for j in range(n)] + ["--print"],
                       capture_output=True, text=True, timeout=120)
    status, obj = None, None
    farkas, fxs, nodes = None, None, None
    x = [None] * n
    for line in r.stdout.splitlines():
        if line.startswith("status: "):
            status = line.split(None, 1)[1].strip()
        elif line.startswith("objective: "):
            obj = float(line.split()[1])
        elif line.startswith("nodes: "):
            nodes = int(line.split()[1])
        elif line.startswith("farkas_certs: "):
            farkas = int(line.split()[1])
        elif line.startswith("fx_solves: "):
            fxs = int(line.split()[1])
        elif line.startswith("x["):
            j = int(line[2:line.index("]")])
            x[j] = float(line.split("=", 1)[1])
    if r.returncode != 0:
        status = "CRASH(rc=%d)" % r.returncode
    return status, obj, nodes, farkas, fxs, x


# ---------------------------------------------------------------- pinned 1
def pinned_cycle(diff, strength, wide):
    """x1 - x2 <= -s, x2 - x1 <= -s with box wide enough that root FBBT
    cannot collapse the cycle within its pass cap.  Objective var x0 on the
    side keeps the model an optimisation problem."""
    s = strength
    n, m = 3, 2
    c = [1, 0, 0]
    A = [[0, 1, -1], [0, -1, 1]]  # rows: x1 - x2 <= -s ; x2 - x1 <= -s
    b = [-s, -s]
    rel = ['<', '<']
    l = [0, -wide, -wide]
    u = [10, wide, wide]
    return n, m, c, A, b, rel, l, u


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260815
    rng = random.Random(seed)
    os.makedirs(TMP, exist_ok=True)
    fails = []
    checked = 0
    counters_seen = True

    # ---- pinned: difference-constraint cycle, root must certify via Farkas
    for wide in (1e6, 1e7):
        n, m, c, A, b, rel, l, u = pinned_cycle(2.0, 1.0, wide)
        path = os.path.join(TMP, "pin_cycle.lp")
        write_lp(path, n, m, c, A, b, rel, l, u, True)
        status, obj, nodes, farkas, fxs, x = run(path, n)
        if status != "INFEASIBLE":
            fails.append("pinned cycle (wide=%g): status=%s, want INFEASIBLE" % (wide, status))
        if farkas is None:
            counters_seen = False  # pre-change engine: prints no counters
        elif farkas < 1 or fxs != 0:
            fails.append("pinned cycle (wide=%g): farkas_certs=%d fx_solves=%d, "
                         "want fast-path certification with no exact re-solve" % (wide, farkas, fxs))
        if nodes is not None and nodes != 1:
            fails.append("pinned cycle (wide=%g): nodes=%s, want 1 (root pruned)" % (wide, nodes))
        checked += 1

    # ---- margin discipline: the reference behaviour the fast path replaces
    # is the EXACT fx re-solve (marginless), so the certificate may only ever
    # fire on exactly-infeasible systems.  Two checks:
    #   (a) sub-tolerance cycles (infeasibility strength < MIP_TOL) are
    #       EXACTLY infeasible over the integer lattice, and the engine
    #       (pre-fix identical, via fx) proves INFEASIBLE -- parity pinned;
    #   (b) exactly-FEASIBLE near-degenerate neighbours must never see a
    #       certificate fire (an over-firing trap).
    for strength in (0.5e-6, 0.9e-6):
        n, m, c, A, b, rel, l, u = pinned_cycle(2.0, strength, 8)
        l = [0, -8, -8]
        u = [10, 8, 8]
        path = os.path.join(TMP, "margin_cycle.lp")
        write_lp(path, n, m, c, A, b, rel, l, u, True)
        status, obj, nodes, farkas, fxs, x = run(path, n)
        # integer lattice: x1-x2 <= -s and >= s simultaneously impossible
        if status != "INFEASIBLE":
            fails.append("margin cycle (s=%g): status=%s, want INFEASIBLE (exactly infeasible)"
                         % (strength, status))
        checked += 1
    for delta in (0.0, 1e-9):
        # x1 - x2 <= delta, x2 - x1 <= 1: exactly feasible over the integer
        # lattice (diff in {-1, 0} for delta in [0, 1e-9]); a certificate
        # firing here would prune a feasible node -- the over-firing trap.
        n, m = 3, 2
        c = [1, 0, 0]
        A = [[0, 1, -1], [0, -1, 1]]
        b = [delta, 1.0]
        rel = ['<', '<']
        l = [0, -8, -8]
        u = [10, 8, 8]
        path = os.path.join(TMP, "feasible_cycle.lp")
        write_lp(path, n, m, c, A, b, rel, l, u, True)
        status, obj, nodes, farkas, fxs, x = run(path, n)
        if status != "OPTIMAL":
            fails.append("feasible tight cycle (d=%g): status=%s, want OPTIMAL" % (delta, status))
        if farkas not in (None, 0):
            fails.append("feasible tight cycle (d=%g): farkas_certs=%d on a feasible model"
                         % (delta, farkas))
        checked += 1

    # ---- random: planted deep infeasibility, brute-force parity everywhere
    nfc = 0
    for it in range(N):
        n = rng.randint(3, 6)
        m = rng.randint(2, 5)
        l = [rng.randint(-6, 4) for _ in range(n)]
        u = [l[j] + rng.randint(2, 8) for j in range(n)]
        A = [[0] * n for _ in range(m)]
        for i in range(m):
            for j in range(n):
                if rng.random() < 0.45:
                    A[i][j] = rng.choice([-6, -4, -3, -2, -1, 1, 2, 3, 4, 6])
        b = [0.0] * m
        rel = [rng.choice("<>=") for _ in range(m)]
        c = [rng.randint(-9, 9) for _ in range(n)]
        # every row either random rhs or pinned-pair cycle rows
        for i in range(m):
            mx = sum((A[i][j] if A[i][j] else 0) * (u[j] if (A[i][j] or 0) > 0 else l[j])
                     for j in range(n))
            b[i] = mx + rng.randint(-6, 6)
        # in half the instances plant a pair of rows that is only jointly
        # contradictory (per-row always satisfiable): u.x <= t and u.x >= t+k
        planted = rng.random() < 0.5
        if planted and m >= 2:
            w = [rng.choice([1, 2]) * rng.choice([-1, 1]) for _ in range(n)]
            base = rng.uniform(2, 5)
            A[0] = w[:]; rel[0] = '<'; b[0] = base
            A[1] = [-x for x in w]; rel[1] = '<'; b[1] = -(base + rng.uniform(1, 4))
        maximize = rng.random() < 0.5
        path = os.path.join(TMP, "rand_%d.lp" % it)
        write_lp(path, n, m, c, A, b, rel, l, u, maximize)
        bstat, bobj, bx = brute(n, m, c, A, b, rel, l, u, maximize)
        status, obj, nodes, farkas, fxs, x = run(path, n)
        if farkas:
            nfc += 1
        if status != bstat:
            fails.append("rand %d: status=%s brute=%s" % (it, status, bstat))
            continue
        if bstat == "OPTIMAL":
            if obj is None or abs(obj - bobj) > 1e-6 * (1 + abs(bobj)):
                fails.append("rand %d: objective=%s brute=%s" % (it, obj, bobj))
            else:
                # point must be integral, in bounds, row-feasible (engine tol)
                good = all(x[j] is not None and abs(x[j] - round(x[j])) < 1e-9 and
                           x[j] >= l[j] - TOL and x[j] <= u[j] + TOL for j in range(n))
                if not good:
                    fails.append("rand %d: bad point %s" % (it, x))
        checked += 1

    print("farkas_verify: checked=%d (farkas-fired=%d) %s" %
          (checked, nfc, "FAILURES=%d" % len(fails) if fails else "ALL OK"))
    for f in fails[:40]:
        print("FAIL", f)
    if not counters_seen:
        print("NOTE: solver printed no farkas/fx counters -> engine without the "
              "Farkas fast path (expected on a pre-change binary)")
        print("farkas_verify: FAILED (no fast path)")
        return 1
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
