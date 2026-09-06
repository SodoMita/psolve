#!/usr/bin/env python3
"""Ruiz equilibration + geometric-mean scaling gate (roadmap 7.5).

The LP CLI now equilibrates the working image by default (strictly
positive diagonal preconditioning Dr·A·Dc; every funnel composes the
diagonals back so caller-visible values stay in ORIGINAL units and every
verdict is still re-certified against ORIGINAL data by the psv lanes).
CLI escape hatches make the change observable and reversible:
--noscale (no equilibration) and --scalestat (pre/post
conditioning-spread proxy on stderr).  With roadmap 7.1 (presolve) the
truly-raw data path is --nopresolve --noscale: presolve is on by default,
so this tool's raw lanes pass both flags to mean 'pre-7.x behaviour' (RAW
below).

Contracts enforced here:

  1. A/B parity on well-scaled data: default vs raw (--nopresolve
     --noscale) print the SAME status on every instance; co-OPTIMAL
     objectives agree <= 1e-7 rel.
     (And scipy/HiGHS agrees when importable.)
  2. Conditioning effect on entry-mixed data (per-entry magnitudes over
     1e+-E): the scalestat proxy must not get worse (post <= pre on >=90%
     of runs) and must halve the median spread on high-spread instances.
  3. Fallback no-loss contract: on aggressive spread (E up to 12), a
     raw-path OPTIMAL answer may NEVER be lost by the default path - when
     the scaled run's evidence is not certifiable the CLI re-solves on raw
     data (stderr 'psv: ... re-solving on raw data'); a NUMERICAL raw run
     that stays NUMERICAL by default is fine (honest class), and the
     reverse (rescue) is the feature.
  4. Rescue existence: at lease one seeded entry-mixed instance where the
     raw engine honestly gives up (LU stall -> NUMERICAL_FAILURE) while
     the default scaled path certifies OPTIMAL (scipy-confirmed when
     importable).  Measured at development: ~0.4% of the E=4 family.
  5. Example-file A/B identity: the six shipped examples print identical
     status+objective under default and raw (--nopresolve --noscale).

Discrimination (project calibration rule): the pre-change binary rejects
--noscale/--scalestat/--nopresolve as unknown options, so the A/B-lane
probe fails loudly on it; the family asserts then cannot be satisfied by
any binary without the 7.5/7.1 lanes.

Usage: tools/scale_verify.py [N] [seed]
Env:   LPSOLVE overrides the binary under test.
"""

import math
import os
import random
import re
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from farkas_verify import write_lp  # reuse the canonical writer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LPSOLVE = os.path.abspath(os.environ.get("LPSOLVE", os.path.join(ROOT, "lpsolve")))
TMP = "/tmp/psolve_scaleverify"
os.makedirs(TMP, exist_ok=True)

STAT = re.compile(r"status: (\S+)")
OBJ = re.compile(r"objective: (\S+)")
SCALESTAT = re.compile(
    r"scalestat: mode=(\d+) ratio_pre=(\S+) ratio_post=(\S+) iters=(\d+)")

try:
    import numpy  # noqa: F401
    from scipy.optimize import linprog
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


def gen_modest(rnd):
    """Planted-feasible, well-scaled LP: spread family O(1e1)."""
    n = rnd.randint(6, 12)
    m = rnd.randint(4, 10)
    A = [[(rnd.uniform(-4, 4) if rnd.random() < 0.6 else 0.0)
          for _ in range(n)] for _ in range(m)]
    for j in range(n):
        if all(A[i][j] == 0 for i in range(m)):
            A[rnd.randrange(m)][j] = rnd.uniform(-4, 4)
    x0 = [rnd.uniform(-2, 2) for _ in range(n)]
    rel, b = [], []
    for i in range(m):
        ax = sum(A[i][j] * x0[j] for j in range(n))
        r = rnd.choice("<><=>")
        if r == '<':
            b.append(ax + rnd.uniform(0.1, 5))
        elif r == '>':
            b.append(ax - rnd.uniform(0.1, 5))
        else:
            b.append(ax)
        rel.append(r)
    l = [x0[j] - rnd.uniform(1, 30) for j in range(n)]
    u = [x0[j] + rnd.uniform(1, 30) for j in range(n)]
    c = [rnd.uniform(-5, 5) for _ in range(n)]
    return n, m, c, A, b, rel, l, u, rnd.random() < 0.5


def gen_mixed(rnd, E):
    """Entry-mixed LP, per-entry magnitudes log-uniform over 1e+-E.
    x=0 is planted strictly feasible (sense-matched b), boxes are wide."""
    n = rnd.randint(6, 12)
    m = rnd.randint(4, 10)
    A = [[(rnd.uniform(-1, 1) * 10 ** rnd.uniform(-E, E))
          if rnd.random() < 0.7 else 0.0 for _ in range(n)] for _ in range(m)]
    for j in range(n):
        if all(A[i][j] == 0 for i in range(m)):
            A[rnd.randrange(m)][j] = rnd.uniform(-1, 1) * 10 ** rnd.uniform(-E, E)
    rel, b = [], []
    for i in range(m):
        r = rnd.choice("<>><")
        rel.append(r)
        mag = 10 ** rnd.uniform(-3, 3)
        b.append(mag if r == '<' else -mag)
    l = [-10 ** rnd.uniform(0, 6) for _ in range(n)]
    u = [10 ** rnd.uniform(0, 6) for _ in range(n)]
    c = [rnd.uniform(-1, 1) * 10 ** rnd.uniform(-E, E) for _ in range(n)]
    return n, m, c, A, b, rel, l, u, rnd.random() < 0.5


RAW = ("--nopresolve", "--noscale")  # the pre-7.x data path (7.1: presolve off)


def run(path, extra=()):
    p = subprocess.run([LPSOLVE, *extra, path],
                       capture_output=True, text=True, timeout=60)
    st = STAT.search(p.stdout)
    ob = OBJ.search(p.stdout)
    return (st.group(1) if st else "PARSE_FAIL",
            float(ob.group(1)) if ob else None,
            p.stderr, p.returncode)


def scalestat(path):
    """First mode=1 scalestat pair (the scaled attempt), else None."""
    p = subprocess.run([LPSOLVE, "--scalestat", path],
                       capture_output=True, text=True, timeout=60)
    for mt in SCALESTAT.finditer(p.stderr):
        if mt.group(1) == "1":
            return float(mt.group(2)), float(mt.group(3))
    return None


def scipy_obj(path):
    if not HAVE_SCIPY:
        return None
    tok = open(path).read().split()
    i = 0
    maximize = tok[i] == "maximize"; i += 1
    n = int(tok[i]); m = int(tok[i + 1]); i += 2
    c = [float(tok[i + k]) for k in range(n)]; i += n
    b = [float(tok[i + k]) for k in range(m)]; i += m
    rel = list(tok[i]); i += 1
    l, u = [], []
    for j in range(n):
        l.append(float(tok[i])); u.append(float(tok[i + 1])); i += 2
    nnz = int(tok[i]); i += 1
    A = [[0.0] * n for _ in range(m)]
    for k in range(nnz):
        r = int(tok[i]); j = int(tok[i + 1]); v = float(tok[i + 2]); i += 3
        A[r][j] = v
    A_ub, b_ub, A_eq, b_eq = [], [], [], []
    for r in range(m):
        if rel[r] == '<':
            A_ub.append(A[r]); b_ub.append(b[r])
        elif rel[r] == '>':
            A_ub.append([-a for a in A[r]]); b_ub.append(-b[r])
        else:
            A_eq.append(A[r]); b_eq.append(b[r])
    obj = [-x for x in c] if maximize else c
    res = linprog(obj, A_ub=A_ub or None, b_ub=b_ub or None,
                  A_eq=A_eq or None, b_eq=b_eq or None,
                  bounds=list(zip(l, u)), method="highs")
    if res.status != 0:
        return None
    return -res.fun if maximize else res.fun


def rel_diff(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1e-300)


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = sys.argv[2] if len(sys.argv) > 2 else "20260818"
    fails = []

    # ---- 0. discrimination probe: the A/B lanes must exist -------------
    probe_path = os.path.join(TMP, "probe.lp")
    write_lp(probe_path, *gen_modest(random.Random(f"{seed}:probe")))
    st, _, err, rc = run(probe_path, RAW)
    st2, _, err2, rc2 = run(probe_path, ("--scalestat",))
    if rc != 0 or "unknown option" in err or rc2 != 0 or \
            "scalestat:" not in err2:
        print("scale_verify: FAIL - binary lacks the 7.5/7.1 A/B lanes "
              "(--nopresolve/--noscale/--scalestat rejected); "
              "tool cannot discriminate")
        sys.exit(1)
    if st != st2:
        fails.append(f"lane probe: --noscale {st} vs --scalestat {st2}")

    # ---- 1. parity on well-scaled data (+ scipy oracle) -----------------
    rnd = random.Random(f"{seed}:modest")
    bad = 0
    oracle_checked = 0
    for t in range(N):
        path = os.path.join(TMP, "modest.lp")
        write_lp(path, *gen_modest(rnd))
        sd, od, _, _ = run(path)
        sr, orr, _, _ = run(path, RAW)
        if sd != sr or (sd == "OPTIMAL" and sr == "OPTIMAL" and
                        rel_diff(od, orr) > 1e-7):
            bad += 1
            fails.append(f"parity t={t}: default {sd}/{od} vs raw {sr}/{orr}")
        if sd == "OPTIMAL" and HAVE_SCIPY:
            want = scipy_obj(path)
            if want is not None:
                oracle_checked += 1
                if rel_diff(od, want) > 1e-6:
                    fails.append(f"oracle t={t}: default {od} vs scipy {want}")
        if len(fails) > 12:
            break
    print(f"  parity family (well-scaled, N={N}): mismatches={bad} "
          f"scipy-checked={oracle_checked}")

    # ---- 2+3+4. entry-mixed families: conditioning, no-loss, rescue -----
    total_rescued = 0
    for E, nE in ((4, N), (12, max(50, N // 3))):
        rnd = random.Random(f"{seed}:mixed:{E}")
        lost = rescued = fallbacks = flips = objbad = 0
        ratios = []
        for t in range(nE):
            path = os.path.join(TMP, f"mixed{E}.lp")
            write_lp(path, *gen_mixed(rnd, E))
            sd, od, ed, _ = run(path)
            sr, orr, _, _ = run(path, RAW)
            if sr == "OPTIMAL" and sd != "OPTIMAL":
                lost += 1
                fails.append(f"no-loss E={E} t={t}: raw OPTIMAL {orr}, "
                             f"default {sd}")
            if sr == "OPTIMAL" and sd == "OPTIMAL" and \
                    rel_diff(od, orr) > 1e-6:
                objbad += 1
                fails.append(f"co-OPT obj E={E} t={t}: default {od} "
                             f"vs raw {orr}")
            if sr != "OPTIMAL" and sd == "OPTIMAL":
                rescued += 1
                if HAVE_SCIPY:
                    want = scipy_obj(path)
                    if want is not None and rel_diff(od, want) > 1e-6:
                        fails.append(f"rescue obj E={E} t={t}: default {od} "
                                     f"vs scipy {want}")
            if sd != sr:
                flips += 1
            if "re-solving on raw data" in ed:
                fallbacks += 1
            rp = scalestat(path)
            if rp is not None:
                ratios.append(rp)
            if len(fails) > 12:
                break
        worse = sum(1 for pre, post in ratios if post > pre * 1.0001)
        hi = [post / pre for pre, post in ratios if pre >= 1e4]
        med = statistics.median(hi) if hi else 1.0
        print(f"  mixed E={E} (N={nE}): flips={flips} lost={lost} "
              f"rescued={rescued} fallbacks={fallbacks} obj>1e-6={objbad} "
              f"spread post>pre={worse}/{len(ratios)} "
              f"median post/pre(hi)={med:.4g}")
        if ratios and worse > 0.10 * len(ratios):
            fails.append(f"conditioning E={E}: spread worsened on "
                         f"{worse}/{len(ratios)} runs")
        if hi and med > 0.5:
            fails.append(f"conditioning E={E}: median post/pre {med:.4g} "
                         f"> 0.5 on high-spread instances")
        total_rescued += rescued
    if total_rescued < 1:
        fails.append(f"rescue existence: 0 rescues across the seeded "
                     f"entry-mixed families (development baselines: ~0.4% "
                     f"at E=4, ~7% at E=12)")

    # ---- 5. example files A/B identity -----------------------------------
    exdir = os.path.join(ROOT, "examples")
    exbad = 0
    for name in sorted(os.listdir(exdir)):
        if not name.endswith(".lp"):
            continue
        p = os.path.join(exdir, name)
        sd, od, _, _ = run(p)
        sr, orr, _, _ = run(p, RAW)
        if sd != sr or (od or 0) != (orr or 0):
            if sd == "OPTIMAL" and sr == "OPTIMAL" and \
                    rel_diff(od, orr) <= 1e-9:
                continue
            exbad += 1
            fails.append(f"examples {name}: default {sd}/{od} vs raw {sr}/{orr}")
    print(f"  examples A/B: mismatches={exbad}")

    if fails:
        print("scale_verify: FAIL")
        for f in fails[:20]:
            print("   ", f)
        sys.exit(1)
    print(f"scale_verify: OK (N={N}, seed={seed}, "
          f"scipy={'yes' if HAVE_SCIPY else 'no'})")


if __name__ == "__main__":
    main()
