#!/usr/bin/env python3
"""Per-model status/objective comparison between two qpsolve binaries over a
qp_diff.py sweep -- the A/B tool behind docs/CURV_PS_PLAN.md 1.12's claim that a
solver change moved only what it meant to move.

`tools/qp_diff.py` answers "is this solver wrong?"; this answers the question a
change review actually asks: *which* models did the change move, and did any
objective change?  It drives both binaries over the same generated models (same
kinds, same order, same seeds as qp_diff.py) and reports every model whose status
or objective differs, with the two verdicts side by side.

usage: qp_status_diff.py N SEED EXE_A EXE_B [lpfirst]
       (lpfirst = run both with PSOLVE_QP_PHASE1_LP_FIRST=1)

Exit code is 0 either way: the differences are the *output*, not a failure.
"""
import importlib.util, os, subprocess, sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/psolve_qpstatusdiff"


def load_qp_diff():
    """Reuse qp_diff.py's model generator verbatim -- the point of the tool is
    to compare the two binaries on qp_diff's own families, not on new ones."""
    spec = importlib.util.spec_from_file_location(
        "qp_diff", os.path.join(ROOT, "tools", "qp_diff.py"))
    qd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qd)
    return qd


def run(exe, path, env):
    r = subprocess.run([exe, path], capture_output=True, text=True, env=env,
                       timeout=600)
    status, obj = None, None
    for ln in r.stdout.splitlines():
        if ln.startswith("STATUS"):
            status = int(ln.split()[1])
        elif ln.startswith("OBJ"):
            obj = float(ln.split()[1])
    if status is None:                     # status 0 prints SOLUTION/OBJ, no STATUS
        status = 0
    return status, obj, r.returncode


def main():
    if len(sys.argv) < 5:
        print(__doc__.strip())
        return 2
    N, seed = int(sys.argv[1]), int(sys.argv[2])
    exe_a, exe_b = sys.argv[3], sys.argv[4]
    lpfirst = len(sys.argv) > 5 and sys.argv[5] == "lpfirst"
    env = dict(os.environ)
    if lpfirst:
        env["PSOLVE_QP_PHASE1_LP_FIRST"] = "1"

    qd = load_qp_diff()
    os.makedirs(TMP, exist_ok=True)
    path = os.path.join(TMP, "case.qp")     # single fixed temp: runs must be serial
    rng = np.random.default_rng(seed)
    kinds = ["pd", "singular", "zero", "diag0"]

    moved, objmov, same = [], [], 0
    for it in range(N):
        kind = kinds[it % len(kinds)]
        n, m, Q, c, A, b = qd.make_case(rng, kind)
        qd.write_qp(path, n, m, Q, c, A, b)
        sa, oa, rca = run(exe_a, path, env)
        sb, ob, rcb = run(exe_b, path, env)
        if sa != sb or rca != rcb:
            moved.append((it, kind, sa, rca, sb, rcb, oa, ob))
        elif sa == 0 and oa is not None and ob is not None \
                and abs(oa - ob) > 1e-9 * (1.0 + abs(oa)):
            objmov.append((it, kind, oa, ob))
        else:
            same += 1

    print("qp_status_diff: %s vs %s  (N=%d, seed=%d, %s)"
          % (os.path.basename(exe_a), os.path.basename(exe_b), N, seed,
             "LP-first" if lpfirst else "default order"))
    print("  identical on %d/%d models" % (same, N))
    print("  status moved on %d:" % len(moved))
    for it, kind, sa, rca, sb, rcb, oa, ob in moved:
        print("    it=%-4d [%-8s] status %d%s -> %d%s   (obj %s -> %s)"
              % (it, kind, sa, " (exit %d)" % rca if rca else "",
                 sb, " (exit %d)" % rcb if rcb else "",
                 "%.17g" % oa if oa is not None else "none",
                 "%.17g" % ob if ob is not None else "none"))
    print("  objective moved on %d (both status 0):" % len(objmov))
    for it, kind, oa, ob in objmov:
        print("    it=%-4d [%-8s] %.12g -> %.12g  (rel %.2g)"
              % (it, kind, oa, ob, abs(oa - ob) / (1.0 + abs(oa))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
