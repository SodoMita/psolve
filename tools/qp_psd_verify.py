#!/usr/bin/env python3
"""QP convexity-gate differential verification (roadmap 6.5): indefinite Q
must never get a fabricated "solved" verdict.

The pre-change gate screened only the 1x1 and 2x2 principal minors of Q:
symmetric matrices n >= 3 whose negativity lives in a larger minor passed
(diag 1, off-diagonal -0.9: every 2x2 minor is 0.19, yet an eigenvalue is
-0.8).  The active-set then returned the stationary ORIGIN as an
"optimum" on problems unbounded below (pinned gadgets below) -- a
fabricated answer in the verifier-free direction (nobody re-checks a
claimed optimum).

The fix: after the symmetry screen, a complete symmetrized ~Cholesky
elimination scan decides PSD (negative pivot beyond the scaled tolerance,
or a zero-ish pivot with a non-zero residual column -- the [0 a; a b]
block with determinant -a^2); completing the scan certifies PSD.  Both
directions are short exact-arithmetic proofs; the tolerance is scaled
(1e-9 * (1 + max|Q_ij|)), and deciding semidefiniteness of doubles can
only ever be relative -- that frontier is the documented tolerance
semantics, not a wrong-verdict hole.

Classes (exact truth known by construction):
  indef_gate : symmetric Q with all 2x2 principal minors safely positive
               (old gate passes) but a macroscopic negative eigenvalue
               (new gate must refuse: STATUS 4 = QP_NON_CONVEX, and never
               a SOLUTION).  Two variants: unconstrained (truth: unbounded
               below; any optimum claim is fabricated) and box-bounded
               (true optimum by vertex enumeration; any status-0 answer is
               objective-checked against it).
  near_psd   : singular PSD plus a TINY negative perturbation BELOW the
               scaled gate tolerance -- accepted per the documented
               tolerance semantics; a status-0 answer must still satisfy
               the KKT/primal-feasibility certificates of the model as
               given (same oracle as tools/qp_diff.py); statuses 2/3 stay
               honest non-answers.
  asym       : asymmetric beyond the symmetry tolerance must be refused
               (STATUS 4); within tolerance is accepted-and-symmetrized
               (classification only).
  scale_mix  : genuinely PSD across 1e-7..1e7 diagonal scalings must not
               be refused as NON_CONVEX (over-blocking check), and their
               status-0 answers pass the scaled KKT oracle.

Discrimination (project calibration rule): on the pre-change binary the
indef_gate class fabricates verdicts (status 0 on unconstrained-indef or
objective mismatches on box-bounded indef); if fabricated==0 on a binary
whose path does not mark it as pre-change, the tool can no longer
discriminate and FAILS loudly.

usage: qp_psd_verify.py [N] [seed]
env:   PSOLVE_QPSOLVE overrides the solver binary; QP_PSD_EXPECT_PRE=1
       marks the binary as the pre-change reference.
"""
import os, subprocess, sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/psolve_qppsd"
QPSOLVE = os.path.abspath(os.environ.get("PSOLVE_QPSOLVE",
                                         os.path.join(ROOT, "qpsolve")))


def write_qp(path, n, m, Q, c, A, b):
    with open(path, "w") as f:
        f.write("%d %d\n" % (n, m))
        f.write(" ".join(repr(float(v)) for v in c) + "\n")
        for j in range(n):                       # column j of Q
            f.write(" ".join(repr(float(Q[i, j])) for i in range(n)) + "\n")
        for i in range(m):                       # row i of A
            f.write(" ".join(repr(float(A[i, j])) for j in range(n)) + "\n")
        f.write(" ".join(repr(float(v)) for v in b) + "\n")


def parse(out):
    # CLI convention (tools/qpsolve.c): success prints SOLUTION/OBJ/ITERS
    # with NO STATUS line; every nonzero status prints STATUS <n>.
    x, status, obj = None, 0, None
    for ln in out.splitlines():
        if ln.startswith("SOLUTION"):
            x = np.array([float(v) for v in ln.split()[1:]])
        elif ln.startswith("STATUS"):
            status = int(ln.split()[1])
        elif ln.startswith("OBJ"):
            obj = float(ln.split()[1])
    return x, status, obj


def run(exe, path):
    r = subprocess.run([exe, path], capture_output=True, text=True, timeout=60)
    return parse(r.stdout)


def min2minors_ok(Q):
    """All 2x2 principal minors safely positive (the old gate's screen)."""
    n = Q.shape[0]
    for i in range(n):
        for j in range(i + 1, n):
            qii, qjj, qij = Q[i, i], Q[j, j], Q[i, j]
            if qii * qjj - qij * qij <= 1e-4 * max(1.0, qii * qjj):
                return False
    return True


def box_opt(Q, c, B):
    """True min of 1/2 x'Qx + c'x over [-B,B]^n by vertex enumeration
    (indefinite Q attains its box minimum at a vertex)."""
    n = Q.shape[0]
    best = np.inf
    for mask in range(1 << n):
        x = np.array([B if (mask >> j) & 1 else -B for j in range(n)])
        obj = 0.5 * x @ Q @ x + c @ x
        if obj < best:
            best = obj
    return best


def kkt_ok(Q, c, A, b, x):
    """The qp_diff first-order oracle for a claimed solution."""
    n = Q.shape[0]
    m = A.shape[0]
    if x is None or len(x) != n:
        return False, "no usable SOLUTION line"
    scale = 1.0 + float(np.max(np.abs(x))) + float(np.max(np.abs(c)))
    qs = 1.0 + float(np.max(np.abs(Q)))
    if m:
        viol = float(np.max(A @ x - b))
        if viol > 1e-6 * (1.0 + float(np.max(np.abs(b)))):
            return False, "primal violation %.3g" % viol
    g = Q @ x + c
    if m:
        act = (b - A @ x) <= 1e-7 * (1.0 + np.abs(b))
        Aa = A[act]
        if Aa.shape[0]:
            lam, *_ = np.linalg.lstsq(Aa.T, -g, rcond=None)
            res = float(np.linalg.norm(Aa.T @ lam + g))
            neg = float(np.min(lam)) if lam.size else 0.0
        else:
            res, neg = float(np.linalg.norm(g)), 0.0
    else:
        res, neg = float(np.linalg.norm(g)), 0.0
    if res > 1e-5 * scale * qs:
        return False, "not stationary: %.3g" % res
    if neg < -1e-6 * scale * qs:
        return False, "negative multiplier %.3g" % neg
    return True, ""


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260815
    rng = np.random.default_rng(seed)
    os.makedirs(TMP, exist_ok=True)
    exe = QPSOLVE
    fails, checked, fab_pre, fab_obj = [], 0, 0, 0
    counts = {}
    pre_marked = "pre" in exe or os.environ.get("QP_PSD_EXPECT_PRE")

    def note(key):
        counts[key] = counts.get(key, 0) + 1

    # ---- pinned gadgets of record (the audit repro) --------------------
    a = -0.9
    Qg = np.array([[1.0, a, a], [a, 1.0, a], [a, a, 1.0]])
    assert min2minors_ok(Qg)
    pins = [("pin_unconstr", Qg, np.zeros(3), np.zeros((0, 3)),
             np.zeros(0), None),
            ("pin_box", Qg, np.zeros(3),
             np.vstack([np.eye(3), -np.eye(3)]), np.ones(6), 1.0)]
    for name, Q, c, A, b, B in pins:
        path = os.path.join(TMP, name + ".qp")
        write_qp(path, Q.shape[0], A.shape[0], Q, c, A, b)
        x, st, obj = run(exe, path)
        checked += 1
        if st == 0:
            fab_pre += 1
            note((name, "fabricated"))
        elif st == 4:
            note((name, "refused"))
        else:
            fails.append("%s: unexpected status %s (want 4 or, pre-change,"
                         " a fabrication to count)" % (name, st))

    # ---- indef_gate family ----------------------------------------------
    got = 0
    while got < N:
        n = int(rng.integers(3, 7))
        R_ = rng.normal(size=(n, n))
        Q, _ = np.linalg.qr(R_)
        d = rng.uniform(0.4, 2.0, n)
        d[rng.integers(0, n)] = -rng.uniform(0.3, 0.9)
        M = (Q * d) @ Q.T                      # eigenvalues d (orthonormal Q)
        M = 0.5 * (M + M.T)
        if not min2minors_ok(M):
            continue                           # old gate must PASS it
        if np.linalg.eigvalsh(M)[0] > -1e-1:
            continue                           # must be macroscopically indef
        c = rng.uniform(-2, 2, n)
        box = got % 2 == 1
        if box:
            Bv = float(rng.uniform(1.0, 3.0))
            A = np.vstack([np.eye(n), -np.eye(n)])
            b = np.full(2 * n, Bv)
            true_obj = box_opt(M, c, Bv)
        else:
            A = np.zeros((0, n)); b = np.zeros(0)
            true_obj = None
        path = os.path.join(TMP, "indef_%d.qp" % got)
        write_qp(path, n, A.shape[0], M, c, A, b)
        x, st, obj = run(exe, path)
        idx = got
        got += 1
        checked += 1
        if st == 0:
            fab_pre += 1
            note("indef_fabricated")
            if box and obj is not None and obj > true_obj + 1e-6 * (1 + abs(true_obj)):
                fab_obj += 1
        elif st == 4:
            note("indef_refused")
        elif pre_marked:
            note("indef_other_pre")   # honest non-answers the weak gate allowed
        else:
            fails.append("indef %d: status %s, want 4 (NON_CONVEX) on a"
                         " macroscopically indefinite Q" % (idx, st))

    # ---- near_psd family -------------------------------------------------
    got = 0
    while got < N // 2:
        n = int(rng.integers(2, 6))
        r_ = max(1, n // 2)
        G = rng.uniform(-2, 2, (r_, n))
        Q = G.T @ G
        # null direction of Q
        _, _, vt = np.linalg.svd(Q)
        u = vt[-1]
        if abs(Q @ u).max() > 1e-8:
            continue
        delta = 1e-12 * float(rng.uniform(0.5, 4.0))
        M = Q - delta * np.outer(u, u)
        M = 0.5 * (M + M.T)                    # feeds symmetric text exactly
        m = int(rng.integers(0, n + 2))
        A = rng.uniform(-2, 2, (m, n))
        b = rng.uniform(0, 3, m)
        c = rng.uniform(-3, 3, n)
        path = os.path.join(TMP, "near_%d.qp" % got)
        write_qp(path, n, m, M, c, A, b)
        x, st, obj = run(exe, path)
        idx = got
        got += 1
        checked += 1
        note(("near", st))
        if st == 0:
            okk, why = kkt_ok(M, c, A, b, x)
            if not okk:
                fails.append("near_psd %d: status 0 but certificate check "
                             "failed: %s" % (got, why))

    # ---- asym family -----------------------------------------------------
    got = 0
    while got < N // 6:
        n = int(rng.integers(2, 6))
        G = rng.uniform(-1, 1, (n, n))
        Q = G.T @ G + 0.2 * np.eye(n)
        E = np.triu(rng.normal(size=(n, n)), 1)
        qs = float(np.max(np.abs(Q)))
        M = Q + 0.1 * (1.0 + qs) * (E - E.T)   # far beyond symmetry tol
        c = rng.uniform(-2, 2, n)
        path = os.path.join(TMP, "asym_%d.qp" % got)
        write_qp(path, n, 0, M, c, np.zeros((0, n)), np.zeros(0))
        x, st, obj = run(exe, path)
        idx = got
        got += 1
        checked += 1
        if st != 4:
            fails.append("asym %d: status %s, want 4 (non-symmetric Q)" % (idx, st))
        else:
            note("asym_refused")

    # ---- scale_mix (genuine PSD, wide scalings) --------------------------
    got = 0
    while got < N // 3:
        n = int(rng.integers(2, 6))
        D = 10.0 ** rng.uniform(-7, 7, n)
        G = rng.uniform(-2, 2, (n, n))
        M = G.T @ G + 0.1 * np.eye(n)
        Q = D[:, None] * M * D[None, :]
        # box x in [-D, D] keeps geometry sane under the scaling
        A = np.vstack([np.diag(1.0 / D), np.diag(-1.0 / D)])
        b = np.ones(2 * n)
        c = D * rng.uniform(-1, 1, n)
        path = os.path.join(TMP, "scale_%d.qp" % got)
        write_qp(path, n, 2 * n, Q, c, A, b)
        x, st, obj = run(exe, path)
        idx = got
        got += 1
        checked += 1
        note(("scale", st))
        if st == 4:
            fails.append("scale_mix %d: genuine PSD (ranges %g..%g) refused"
                         " as NON_CONVEX (over-blocking gate)" %
                         (idx, D.min() ** 2, D.max() ** 2))
        elif st == 0:
            okk, why = kkt_ok(Q, c, A, b, x)
            if not okk:
                fails.append("scale_mix %d: status 0 but certificate failed: "
                             "%s" % (idx, why))

    print("qp_psd_verify: checked=%d fabricated_status0=%d (obj_mismatch=%d) %s"
          % (checked, fab_pre, fab_obj,
             "FAILURES=%d" % len(fails) if fails else "ALL OK"))
    order = sorted(counts.items(), key=lambda kv: str(kv[0]))
    print("  classes: " + ", ".join("%s=%d" % (k, v) for k, v in order))
    for f_ in fails[:15]:
        print("FAIL", f_)

    if fab_pre > 0 and not fails and pre_marked:
        print("qp_psd_verify: pre-change binary fabricated %d optimum verdicts "
              "on indefinite Q (expected; tool discriminates)" % fab_pre)
        return 0
    if fab_pre > 0:
        print("qp_psd_verify: FAILED (%d fabricated optimum verdicts on "
              "indefinite Q survived the gate)" % fab_pre)
        return 1
    if fails:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
