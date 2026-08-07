#!/usr/bin/env python3
"""QP status + KKT-certificate differential test.

tools/qp_gen.py only ever builds a strictly positive-definite Q (G^T G + 0.1 I)
with a mostly origin-feasible polytope, and it verifies by comparing the
objective with SLSQP.  That leaves the interesting cases untested: singular but
still convex Q (the header promises PSD is accepted), Q = 0, no constraints at
all, duplicated/redundant rows, and genuinely infeasible systems.  It also
cannot distinguish "the solver is wrong" from "SLSQP did not converge".

This test uses an oracle that does not depend on another optimizer: for a
convex QP the KKT conditions are necessary *and sufficient*, so a reported
solution is checked directly for

    primal feasibility     A x <= b
    dual feasibility       lambda >= 0
    complementarity        lambda_i (b_i - (Ax)_i) = 0
    stationarity           Q x + c + A^T lambda = 0

and a reported status of "no feasible start" is cross-checked against an exact
LP feasibility test (scipy linprog/HiGHS).  Anything the solver refuses to
answer (iteration limit, KKT not certified) is counted separately: those are
honest non-answers, not wrong answers.

usage: qp_diff.py [N] [seed]
"""
import os, subprocess, sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/psolve_qpdiff"


def write_qp(path, n, m, Q, c, A, b):
    with open(path, "w") as f:
        f.write("%d %d\n" % (n, m))
        f.write(" ".join(repr(float(v)) for v in c) + "\n")
        for j in range(n):                       # column j of Q
            f.write(" ".join(repr(float(Q[i, j])) for i in range(n)) + "\n")
        for i in range(m):                       # row i of A
            f.write(" ".join(repr(float(A[i, j])) for j in range(n)) + "\n")
        f.write(" ".join(repr(float(v)) for v in b) + "\n")


def make_case(rng, kind):
    n = int(rng.integers(1, 7))
    m = int(rng.integers(0, n + 3))
    if kind == "pd":
        G = rng.uniform(-2, 2, (n, n))
        Q = G.T @ G + 0.1 * np.eye(n)
    elif kind == "singular":
        r = max(1, n // 2)
        G = rng.uniform(-2, 2, (r, n))
        Q = G.T @ G                              # PSD, rank r < n
    elif kind == "zero":
        Q = np.zeros((n, n))                     # pure LP through the QP path
    elif kind == "diag0":
        d = rng.uniform(0, 3, n)
        d[rng.integers(0, n)] = 0.0              # one flat direction
        Q = np.diag(d)
    else:
        raise AssertionError(kind)
    c = rng.uniform(-5, 5, n)
    A = rng.uniform(-3, 3, (m, n))
    if m >= 2 and rng.random() < 0.3:
        A[-1] = A[0]                             # duplicate row (degenerate)
    if rng.random() < 0.25:
        b = rng.uniform(-5, 5, m)                # origin often infeasible
    else:
        b = rng.uniform(0, 5, m)
    return n, m, Q, c, A, b


def feasible_lp(A, b, n):
    """Is {x : A x <= b} nonempty?  Exact-ish via HiGHS."""
    from scipy.optimize import linprog
    if A.shape[0] == 0:
        return True
    r = linprog(np.zeros(n), A_ub=A, b_ub=b, bounds=[(None, None)] * n,
                method="highs")
    return r.status != 2


def bounded_below(Q, c, A, b, n):
    """Is the convex QP bounded below on the feasible set?  Unbounded only if
    there is a recession direction d with A d <= 0, Q d = 0 and c.d < 0."""
    from scipy.optimize import linprog
    m = A.shape[0]
    # minimize c.d subject to A d <= 0, Q d = 0, -1 <= d <= 1
    A_ub = A if m else np.zeros((0, n))
    r = linprog(c, A_ub=A_ub, b_ub=np.zeros(m), A_eq=Q, b_eq=np.zeros(n),
                bounds=[(-1, 1)] * n, method="highs")
    if r.status != 0:
        return None                              # inconclusive
    return r.fun > -1e-9


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 4242
    rng = np.random.default_rng(seed)
    os.makedirs(TMP, exist_ok=True)
    path = os.path.join(TMP, "case.qp")
    exe = os.environ.get("PSOLVE_QPSOLVE", os.path.join(ROOT, "qpsolve"))

    kinds = ["pd", "singular", "zero", "diag0"]
    bad, counts, checked = [], {}, 0
    for it in range(N):
        kind = kinds[it % len(kinds)]
        n, m, Q, c, A, b = make_case(rng, kind)
        write_qp(path, n, m, Q, c, A, b)
        try:
            r = subprocess.run([exe, path], capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            bad.append((it, kind, "TIMEOUT"))
            continue
        if r.returncode != 0:
            bad.append((it, kind, "exit %d %s" % (r.returncode, r.stderr.strip()[:60])))
            continue
        x = None
        status = 0
        for ln in r.stdout.splitlines():
            if ln.startswith("SOLUTION"):
                x = np.array([float(v) for v in ln.split()[1:]])
            elif ln.startswith("STATUS"):
                status = int(ln.split()[1])
        counts[(kind, str(status))] = counts.get((kind, str(status)), 0) + 1

        if status == -1:                        # "no feasible start"
            checked += 1
            if feasible_lp(A, b, n):
                bad.append((it, kind, "said infeasible, but A x <= b is feasible"))
            continue
        if status == 1:                         # unbounded
            checked += 1
            bnd = bounded_below(Q, c, A, b, n)
            if bnd is True:
                bad.append((it, kind, "said unbounded, but the QP is bounded below"))
            continue
        if status != 0:
            # Honest non-answers.  Still worth classifying: an iteration limit
            # on a problem that is genuinely unbounded below is morally the
            # right call (just without a certificate), whereas one on a bounded
            # problem is a real capability gap.
            if status == 2:
                bnd = bounded_below(Q, c, A, b, n)
                key = "limit-unbounded" if bnd is False else (
                      "limit-bounded" if bnd is True else "limit-?")
                counts[(kind, key)] = counts.get((kind, key), 0) + 1
            continue

        checked += 1
        # A convex QP that is unbounded below has no optimum at all, so any
        # finite "solution" is wrong.  Stationarity alone does not catch this
        # when the KKT residual is only small to within tolerance.
        if bounded_below(Q, c, A, b, n) is False:
            bad.append((it, kind, "reported an optimum for a QP that is unbounded below"))
            continue
        if x is None or len(x) != n:
            bad.append((it, kind, "status 0 but no usable SOLUTION line"))
            continue
        scale = 1.0 + float(np.max(np.abs(x))) + float(np.max(np.abs(c)))
        # primal feasibility
        if m:
            viol = float(np.max(A @ x - b))
            if viol > 1e-6 * (1.0 + float(np.max(np.abs(b)))):
                bad.append((it, kind, "infeasible point: max A x - b = %.3g" % viol))
                continue
        # KKT: for a convex QP these are necessary and sufficient, so a point
        # that satisfies them is a global optimum -- no second solver needed.
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
        if res > 1e-5 * scale:
            bad.append((it, kind, "not stationary: |Qx+c+A'l| = %.3g (scale %.3g)"
                        % (res, scale)))
            continue
        if neg < -1e-6 * scale:
            bad.append((it, kind, "negative multiplier %.3g (not a minimum)" % neg))
            continue

    print("qp_diff: checked=%d WRONG=%d  (N=%d, seed=%d)" % (checked, len(bad), N, seed))
    order = sorted(counts.items(), key=lambda kv: (kv[0][0], str(kv[0][1])))
    print("  (kind,status): " + ", ".join("%s/%s=%d" % (k[0], k[1], v) for k, v in order))
    for b_ in bad[:15]:
        print("  it=%d [%s] %s" % b_)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
