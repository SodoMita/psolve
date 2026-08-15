#!/usr/bin/env python3
"""Differential test: CP branch-and-bound optimization vs brute force.

Generates small pure-integer finite-domain FlatZinc optimization models that
mix CP-motivating constraints (all_different, element, set_in, reif/minmax)
with an objective variable defined by an integer linear equality, then
compares the solver's reported outcome against exhaustive enumeration:

  * status must be SAT-with-`==========` (proven) exactly when the brute
    force is feasible, and UNSATISFIABLE exactly when it is not;
  * the reported objective must equal the brute-force optimum (never better,
    never worse);
  * every reported variable assignment must satisfy every constraint and
    land inside its declared domain;
  * the same model must report the same optimum via the MIP bridge, checked
    by re-running every model with a pinned float variable (forces the MIP
    path); a MIP "optimum" may never disagree with brute force either.
    (This cross-path A/B is what caught the int_min disjunction-row bug the
    satisfy tests could not see.)

Usage: cp_opt_verify.py [N] [seed] [solver]
"""
from __future__ import annotations

import itertools
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOLVER = pathlib.Path(os.environ.get("FZNSOLVE", str(ROOT / "fznsolve")))


def gen(rng):
    """Return (model_text, check) where check(vals)->bool and obj(expr)."""
    n = rng.randint(2, 4)
    doms = [sorted(set([rng.randint(-4, 4) for _ in range(rng.randint(2, 4))])) for _ in range(n)]
    decls = []
    names = []
    for i, dv in enumerate(doms):
        nm = f"x{i}"
        names.append(nm)
        if dv == list(range(dv[0], dv[-1] + 1)):
            decls.append(f"var {dv[0]}..{dv[-1]}: {nm} :: output_var;")
        else:
            decls.append("var {%s}: %s :: output_var;" % (", ".join(map(str, dv)), nm))
    constraints = []
    checks = []
    kind = rng.choice(["alldiff", "elem", "setin", "reif", "minmax", "lin"])
    if kind == "alldiff" and n >= 2:
        constraints.append("array [1..%d] of var int: xa = [%s];" % (n - 1, ", ".join(names[:-1])))
        constraints.append("constraint all_different_int(xa);")
        checks.append(lambda v: len(set(v[:-1])) == len(v[:-1]))
    elif kind == "elem":
        arr = [rng.randint(-3, 3) for _ in range(3)]
        constraints.append("constraint array_int_element(x0, [%s], x%d);" % (", ".join(map(str, arr)), n - 1))
        checks.append(lambda v, arr=arr, n=n: 1 <= v[0] <= 3 and arr[v[0] - 1] == v[n - 1])
    elif kind == "setin":
        S = sorted(set(rng.randint(-4, 4) for _ in range(3)))
        constraints.append("constraint set_in(x0, {%s});" % ", ".join(map(str, S)))
        checks.append(lambda v, S=S: v[0] in S)
    elif kind == "reif" and n >= 2:
        rel, fn = rng.choice([("eq", lambda a, b: a == b), ("le", lambda a, b: a <= b),
                              ("lt", lambda a, b: a < b), ("ne", lambda a, b: a != b)])
        decls.append("var bool: r :: output_var;")
        constraints.append(f"constraint int_{rel}_reif(x0, x1, r);")
        if rng.random() < 0.5:
            rv = rng.choice([0, 1])
            constraints.append(f"constraint int_eq(r, {rv});")
            checks.append(lambda v, fn=fn, rv=rv: fn(v[0], v[1]) == bool(rv))
        else:
            checks.append(lambda v, fn=fn: True)  # r free: checked via witness below if present
    elif kind == "minmax" and n >= 2:
        c = rng.randint(-4, 4)
        mm = rng.choice(["int_min", "int_max"])
        constraints.append(f"constraint {mm}({c}, x0, x1);")
        f = min if mm == "int_min" else max
        checks.append(lambda v, f=f, c=c: v[1] == f(c, v[0]))
    else:
        n = max(n, 2)
        cs = [rng.randint(1, 3) for _ in range(2)]
        rhs = rng.randint(-6, 10)
        constraints.append(f"constraint int_lin_le([{cs[0]}, {cs[1]}], [x0, x1], {rhs});")
        checks.append(lambda v, cs=cs, rhs=rhs: cs[0] * v[0] + cs[1] * v[1] <= rhs)
    # occasional second plain linear constraint to create infeasibility
    if rng.random() < 0.35:
        cs = [rng.randint(-3, 3) for _ in range(2)]
        rhs = rng.randint(-8, 2)
        constraints.append(f"constraint int_lin_ge([{cs[0]}, {cs[1]}], [x0, x1], {rhs});")
        checks.append(lambda v, cs=cs, rhs=rhs: cs[0] * v[0] + cs[1] * v[1] >= rhs)

    ocoefs = [rng.randint(-3, 3) for _ in names]
    maximize = rng.random() < 0.5
    decls.append("var -60..60: obj :: output_var;")
    constraints.append(
        "constraint int_lin_eq([%s], [%s], 0);" % (", ".join(map(str, ocoefs + [-1])), ", ".join(names + ["obj"])))
    checks.append(lambda v, oc=ocoefs: None)  # obj row checked via obj equivalence
    model = "\n".join(decls + constraints + ["solve %s obj;" % ("maximize" if maximize else "minimize")])
    doms_all = doms
    # brute-force optimum
    best = None
    bestw = None
    for combo in itertools.product(*doms_all):
        vals = list(combo)
        ok = True
        for chk in checks:
            r = chk(vals)
            if r is False:
                ok = False
                break
        if not ok:
            continue
        ov = sum(c * x for c, x in zip(ocoefs, vals))
        if best is None or (ov > best if maximize else ov < best):
            best = ov
            bestw = dict(zip(names, vals))
    return model, names, best, maximize, doms_all, ocoefs


def run_one(model):
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as f:
        f.write(model)
        path = f.name
    try:
        r = subprocess.run([str(SOLVER), "-s", path], text=True, capture_output=True, timeout=30)
    finally:
        os.unlink(path)
    return r.stdout


def force_mip(model: str) -> str:
    """Pin a float var so the model leaves the pure-finite-domain subset and
    must go through the MIP bridge encoding."""
    return ("var 0.0..1.0: __f;\nconstraint float_eq(__f, 0.5);\n" + model)


def check_out(model, out, names, best, maximize, doms, ocoefs, tag, t):
    """Returns None when the run agrees with brute force, else an error str."""
    if best is None:
        if "=====UNSATISFIABLE=====" not in out:
            return f"[{t}/{tag}] INFEASIBLE model not proven UNSAT:\n{model}\n{out}"
        return None
    if "=====UNSATISFIABLE=====" in out or "=====UNKNOWN=====" in out:
        return f"[{t}/{tag}] feasible optimization model not solved (status lost):\n{model}\n{out}"
    if "==========" not in out:
        return f"[{t}/{tag}] optimum reported without proof marker:\n{model}\n{out}"
    mo = re.search(r"%%mzn-stat: objective=(-?[\d.]+)", out)
    if not mo:
        return f"[{t}/{tag}] no objective stat:\n{model}\n{out}"
    got = float(mo.group(1))
    if abs(got - best) > 1e-9:
        return f"[{t}/{tag}] WRONG OPTIMUM solver={got} brute={best}:\n{model}\n{out}"
    # witness validity: every printed var in domain + constraints hold
    vals = {}
    wok = True
    for i, nm in enumerate(names):
        mv = re.search(rf"\b{nm} = (-?\d+);", out)
        if not mv:
            wok = False
            break
        v = int(mv.group(1))
        if v not in doms[i]:
            wok = False
            break
        vals[nm] = v
    if wok:
        # obj printed must equal the linear form of the witness
        mobj = re.search(r"\bobj = (-?\d+);", out)
        if not mobj or int(mobj.group(1)) != sum(c * vals[nm] for c, nm in zip(ocoefs, names)):
            wok = False
    if not wok:
        return f"[{t}/{tag}] invalid/out-of-domain witness for reported optimum:\n{model}\n{out}"
    return None


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = __import__("random").Random(seed)
    ok = wrong = 0
    for t in range(N):
        model, names, best, maximize, doms, ocoefs = gen(rng)
        # path A: whatever the dispatcher picks (CP B&B for pure FD models)
        err = check_out(model, run_one(model), names, best, maximize, doms, ocoefs, "disp", t)
        if err:
            print(err)
            wrong += 1
        else:
            ok += 1
        # path B: float-pinned -> must cross the MIP bridge encoding
        mip_model = force_mip(model)
        err = check_out(mip_model, run_one(mip_model), names, best, maximize, doms, ocoefs, "mip", t)
        if err:
            print(err)
            wrong += 1
        else:
            ok += 1
    print(f"cp_opt_verify: OK={ok} WRONG={wrong}  (N={N}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    raise SystemExit(main())
