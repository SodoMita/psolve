#!/usr/bin/env python3
"""Differential verifier for the Phase-11.2 wave-2 FlatZinc globals added to
the MIP bridge in src/fzn.c (handle_constraint):

  counting     fzn_at_most_int / fzn_at_least_int / fzn_exactly_int (+_reif)
               fzn_at_most1
  precedence   gecode_precede(x,s,t), fzn_value_precede_int(s,t,x),
               fzn_value_precede_chain_int(T,x), fzn_seq_precede_chain_int(x)
  arg-extremum gecode_minimum/maximum_arg_int_offset(x,off,i),
               fzn_minimum/maximum_arg_int (1-based, least index wins ties)
  sort         fzn_sort(x,y), fzn_arg_sort_int(x,p)
  sliding      gecode_among_seq_int(x,S,seq,low,up), gecode_among_seq_bool,
               fzn_sliding_among_int/bool(low,up,seq,x,S|b)
  2D element   gecode_int_element2d(i,j,x,S1,S2,c)

Two independent oracles:
  1. brute force over the declared domains with the exact MiniZinc semantics
     implemented in Python (direct .fzn instances, every var annotated
     output so `-a` prints full assignments; solution SETS must match exactly)
  2. MiniZinc 2.9.5 end-to-end: models compiled with `minizinc -c --solver
     gecode`, fznsolve vs the bundled fzn-gecode oracle (verdicts and -a
     solution sets over the output vars must match)

Verdict contract:
  - these preds are now handled: UNKNOWN is a FAILURE, not honesty
  - UNSATISFIABLE must agree with brute force / gecode (proven)
  - SAT assignment must lie in the brute-forced witness set (verified)
  - optimize objective must equal the brute-forced optimum

Usage: fzn_globals_verify.py [N] [seed]     (N random instances per family)
"""
import itertools
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

FZ = os.environ.get("FZNSOLVE", "./fznsolve")
MINIZINC = os.environ.get("MINIZINC", shutil.which("minizinc") or "/home/user/mz/bin/minizinc")
FZN_GECODE = os.environ.get("FZN_GECODE", "/home/user/mz/bin/fzn-gecode")

PRED_DECLS = """
predicate fzn_at_most_int(int: n, array [int] of var int: x, int: v);
predicate fzn_at_least_int(int: n, array [int] of var int: x, int: v);
predicate fzn_exactly_int(int: n, array [int] of var int: x, int: v);
predicate fzn_at_most_int_reif(int: n, array [int] of var int: x, int: v, var bool: b);
predicate fzn_at_least_int_reif(int: n, array [int] of var int: x, int: v, var bool: b);
predicate fzn_exactly_int_reif(int: n, array [int] of var int: x, int: v, var bool: b);
predicate fzn_at_most1(array [int] of var bool: x, bool: v);
predicate fzn_at_most1_reif(array [int] of var bool: x, bool: v, var bool: b);
predicate gecode_precede(array [int] of var int: x, int: s, int: t);
predicate fzn_value_precede_int(int: s, int: t, array [int] of var int: x);
predicate fzn_value_precede_chain_int(array [int] of int: T, array [int] of var int: x);
predicate fzn_seq_precede_chain_int(array [int] of var int: x);
predicate gecode_minimum_arg_int_offset(array [int] of var int: x, int: offset, var int: i);
predicate gecode_maximum_arg_int_offset(array [int] of var int: x, int: offset, var int: i);
predicate fzn_minimum_arg_int(array [int] of var int: x, var int: i);
predicate fzn_maximum_arg_int(array [int] of var int: x, var int: i);
predicate fzn_sort(array [int] of var int: x, array [int] of var int: y);
predicate fzn_arg_sort_int(array [int] of var int: x, array [int] of var int: p);
predicate gecode_among_seq_int(array [int] of var int: x, set of int: S, int: seq, int: low, int: up);
predicate gecode_among_seq_bool(array [int] of var bool: x, bool: b, int: seq, int: low, int: up);
predicate fzn_sliding_among_int(int: low, int: up, int: seq, array [int] of var int: x, set of int: S);
predicate fzn_sliding_among_bool(int: low, int: up, int: seq, array [int] of var bool: x, bool: b);
predicate gecode_int_element2d(var int: i, var int: j, array [int] of var int: x, set of int: S1, set of int: S2, var int: c);
"""


def run(cmd, timeout=120):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def run_fzn_text(text, *options, timeout=90):
    fd, path = tempfile.mkstemp(suffix=".fzn")
    try:
        os.write(fd, text.encode())
        os.close(fd)
        p = run([FZ, *options, path], timeout)
        return p.stdout
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        os.unlink(path)


# ---------------------------------------------------------------- fzn text
def var(name, lo, hi):
    return f"var {lo}..{hi}: {name} :: output_var;"


def varray(name, n, lo, hi):
    return (f"array[1..{n}] of var {lo}..{hi}: {name}"
            f" :: output_array([1..{n}]);")


def varray_alias(name, elems):
    n = len(elems)
    return (f"array[1..{n}] of var int: {name} :: output_array([1..{n}])"
            f" = [{','.join(elems)}];")


def model(decls, cons, solve="solve satisfy;"):
    return (PRED_DECLS + "\n".join(decls) + "\n"
            + "\n".join("constraint " + c + ";" for c in cons) + "\n" + solve + "\n")


def _tok(t):
    t = t.strip()
    if t == "true":
        return 1
    if t == "false":
        return 0
    return int(t)


def parse_blocks(out):
    blocks = []
    for blk in re.split(r"^----------$", out, flags=re.M):
        vals = {}
        for name, v in re.findall(r"^(\w+) = (-?\d+|true|false);$", blk, re.M):
            vals[name] = _tok(v)
        for name, lo, hi, xs in re.findall(
                r"^(\w+) = array1d\((-?\d+)\.\.(-?\d+), \[([^\]]*)\]\);$", blk, re.M):
            vals[name] = [_tok(t) for t in xs.split(",") if t.strip()]
        if vals:
            blocks.append(vals)
    return blocks


def status_of(out):
    if "=====UNSATISFIABLE=====" in out:
        return "UNSAT"
    if "=====UNKNOWN=====" in out:
        return "UNKNOWN"
    if "----------" in out or "==========" in out:
        return "SAT"
    return "?"


def objective_of(out):
    m = re.search(r"%%mzn-stat: objective=(-?[\d.eE+]+)", out)
    return float(m.group(1)) if m else None


# ------------------------------------------------------- brute-force core
class Inst:
    def __init__(self, text, names, domains, check):
        self.text = text
        self.names = names
        self.domains = domains
        self.check = check

    def brute(self):
        sols = []
        for combo in itertools.product(*[self.domains[k] for k in self.names]):
            asg = dict(zip(self.names, combo))
            if self.check(asg):
                sols.append(asg)
        return sols

    def key(self, asg):
        return tuple(tuple(asg[k]) if isinstance(asg[k], list) else asg[k]
                     for k in self.names)


def arr_domain(n, lo=1, hi=3):
    return [list(c) for c in itertools.product(range(lo, hi + 1), repeat=n)]


# ------------------------------------------------------------ generators
def gen_count(rng):
    n = rng.randint(0, 3)
    ln = rng.randint(2, 5)
    v = rng.randint(1, 3)
    rel = rng.choice(["fzn_at_most_int", "fzn_at_least_int", "fzn_exactly_int"])
    reif = rng.random() < 0.3
    arr = "x"
    decls = [varray(arr, ln, 1, 3)]
    names = [arr]
    doms = {arr: arr_domain(ln, 1, 3)}
    cons = []
    if reif:
        decls.append(var("b", 0, 1))
        names.append("b")
        doms["b"] = [0, 1]
        cons.append(f"{rel}_reif({n}, {arr}, {v}, b)")
    else:
        cons.append(f"{rel}({n}, {arr}, {v})")

    def check(asg):
        c = sum(1 for t in asg[arr] if t == v)
        ok = (c <= n) if "at_most" in rel else (c >= n) if "at_least" in rel else (c == n)
        return asg["b"] == (1 if ok else 0) if reif else ok
    return Inst(model(decls, cons), names, doms, check)


def gen_at_most1(rng):
    ln = rng.randint(2, 5)
    v = rng.randint(0, 1)
    arr = "x"
    reif = rng.random() < 0.3
    decls = [varray(arr, ln, 0, 1)]
    names = [arr]
    doms = {arr: arr_domain(ln, 0, 1)}
    cons = []
    if reif:
        decls.append(var("b", 0, 1)); names.append("b"); doms["b"] = [0, 1]
        cons.append(f"fzn_at_most1_reif({arr}, {v}, b)")
    else:
        cons.append(f"fzn_at_most1({arr}, {v})")

    def check(asg):
        ok = sum(1 for t in asg[arr] if t == v) <= 1
        return asg["b"] == (1 if ok else 0) if reif else ok
    return Inst(model(decls, cons), names, doms, check)


def _precede(x, s, t):
    for j, xv in enumerate(x):
        if xv == t and not any(x[i] == s for i in range(j)):
            return False
    return True


def gen_precede(rng):
    ln = rng.randint(2, 5)
    s, t = rng.randint(1, 3), rng.randint(1, 3)
    arr = "x"
    form = rng.choice(["gecode", "fzn", "chain", "seq"])
    decls = [varray(arr, ln, 1, 3)]
    names = [arr]
    doms = {arr: arr_domain(ln, 1, 3)}
    if form == "gecode":
        cons = [f"gecode_precede({arr}, {s}, {t})"]
        check = lambda a: _precede(a[arr], s, t)
    elif form == "fzn":
        cons = [f"fzn_value_precede_int({s}, {t}, {arr})"]
        check = lambda a: _precede(a[arr], s, t)
    elif form == "chain":
        chain = [rng.randint(1, 3) for _ in range(rng.randint(2, 4))]
        cl = "[" + ",".join(map(str, chain)) + "]"
        cons = [f"fzn_value_precede_chain_int({cl}, {arr})"]
        check = lambda a: all(_precede(a[arr], chain[k], chain[k + 1])
                              for k in range(len(chain) - 1))
    else:
        cons = ["fzn_seq_precede_chain_int(x)"]
        check = lambda a: all(_precede(a[arr], k, k + 1) for k in range(1, ln))
    return Inst(model(decls, cons), names, doms, check)


def gen_arg_extremum(rng):
    ln = rng.randint(2, 4)
    ismax = rng.random() < 0.5
    gec = rng.random() < 0.5
    arr = "x"
    decls = [varray(arr, ln, 1, 3), var("i", 1, ln)]
    names = [arr, "i"]
    doms = {arr: arr_domain(ln, 1, 3), "i": list(range(1, ln + 1))}
    if gec:
        cons = [f"gecode_{'maximum' if ismax else 'minimum'}_arg_int_offset({arr}, 1, i)"]
    else:
        cons = [f"fzn_{'maximum' if ismax else 'minimum'}_arg_int({arr}, i)"]

    def check(asg):
        x, i = asg[arr], asg["i"]
        ext = max(x) if ismax else min(x)
        return i == min(k for k in range(ln) if x[k] == ext) + 1
    return Inst(model(decls, cons), names, doms, check)


def gen_sort(rng):
    ln = rng.randint(2, 4)
    decls = [varray("x", ln, 1, 3), varray("y", ln, 1, 3)]
    names = ["x", "y"]
    doms = {"x": arr_domain(ln, 1, 3), "y": arr_domain(ln, 1, 3)}
    cons = ["fzn_sort(x, y)"]

    def check(asg):
        return asg["y"] == sorted(asg["x"])
    return Inst(model(decls, cons), names, doms, check)


def gen_arg_sort(rng):
    ln = rng.randint(2, 4)
    decls = [varray("x", ln, 1, 3), varray("p", ln, 1, ln)]
    names = ["x", "p"]
    doms = {"x": arr_domain(ln, 1, 3),
            "p": [list(c) for c in itertools.permutations(range(1, ln + 1))]}
    cons = ["fzn_arg_sort_int(x, p)"]

    def check(asg):
        x, p = asg["x"], asg["p"]
        return sorted(p) == list(range(1, ln + 1)) and \
            all(x[p[k] - 1] <= x[p[k + 1] - 1] for k in range(ln - 1))
    return Inst(model(decls, cons), names, doms, check)


def gen_among_seq(rng):
    ln = rng.randint(3, 6)
    seq = rng.randint(1, ln)
    low = rng.randint(0, seq)
    up = rng.randint(low, seq)
    isb = rng.random() < 0.4
    gec = rng.random() < 0.5
    arr = "x"
    lo, hi = (0, 1) if isb else (1, 3)
    decls = [varray(arr, ln, lo, hi)]
    names = [arr]
    doms = {arr: arr_domain(ln, lo, hi)}
    if isb:
        bv = rng.randint(0, 1)
        if gec:
            cons = [f"gecode_among_seq_bool({arr}, {bv}, {seq}, {low}, {up})"]
        else:
            cons = [f"fzn_sliding_among_bool({low}, {up}, {seq}, {arr}, {bv})"]
        S = {bv}
    else:
        if rng.random() < 0.5:
            a, b = rng.randint(1, 2), rng.randint(2, 3)
            S = set(range(a, b + 1))
            slit = f"{a}..{b}"
        else:
            vals = sorted(rng.sample(range(1, 4), rng.randint(1, 3)))
            S = set(vals)
            slit = "{" + ",".join(map(str, vals)) + "}"
        if gec:
            cons = [f"gecode_among_seq_int({arr}, {slit}, {seq}, {low}, {up})"]
        else:
            cons = [f"fzn_sliding_among_int({low}, {up}, {seq}, {arr}, {slit})"]

    def check(asg):
        x = asg[arr]
        return all(low <= sum(1 for i in range(k, k + seq) if x[i] in S) <= up
                   for k in range(0, ln - seq + 1))
    return Inst(model(decls, cons), names, doms, check)


def gen_element2d(rng):
    r = rng.randint(1, 2)
    c = rng.randint(1, 3)
    n = r * c
    decls = [varray("a", n, 1, 3), var("i", 1, r), var("j", 1, c), var("v", 1, 3)]
    names = ["a", "i", "j", "v"]
    doms = {"a": arr_domain(n, 1, 3), "i": list(range(1, r + 1)),
            "j": list(range(1, c + 1)), "v": list(range(1, 4))}
    cons = [f"gecode_int_element2d(i, j, a, 1..{r}, 1..{c}, v)"]

    def check(asg):
        return asg["v"] == asg["a"][(asg["i"] - 1) * c + (asg["j"] - 1)]
    return Inst(model(decls, cons), names, doms, check)


GENS = [gen_count, gen_at_most1, gen_precede, gen_arg_extremum,
        gen_sort, gen_arg_sort, gen_among_seq, gen_element2d]


# ------------------------------------------------------------------ pins
def pins():
    P = []
    # precede UNSAT: x=[2], t=2 present, no earlier s=1
    P.append(("pin_precede_unsat",
              model([var("x1", 2, 2), varray_alias("x", ["x1"])],
                    ["gecode_precede(x, 1, 2)"]),
              ["x1", "x"], {"x1": [2], "x": [[2]]}, lambda a: False))
    # precede SAT: x=[2,1], s=2 before t=1
    P.append(("pin_precede_sat",
              model([var("x1", 2, 2), var("x2", 1, 1), varray_alias("x", ["x1", "x2"])],
                    ["gecode_precede(x, 2, 1)"]),
              ["x1", "x2", "x"], {"x1": [2], "x2": [1], "x": [[2, 1]]},
              lambda a: _precede(a["x"], 2, 1)))
    # at_most(1, x, 1) with x=[1,1] -> UNSAT
    P.append(("pin_atmost_unsat",
              model([var("x1", 1, 1), var("x2", 1, 1), varray_alias("x", ["x1", "x2"])],
                    ["fzn_at_most_int(1, x, 1)"]),
              ["x1", "x2", "x"], {"x1": [1], "x2": [1], "x": [[1, 1]]},
              lambda a: sum(1 for t in a["x"] if t == 1) <= 1))
    # arg_min least index: x=[3,1,1] -> i=2; i=3 pinned -> UNSAT
    P.append(("pin_argmin_least",
              model([var("x1", 3, 3), var("x2", 1, 1), var("x3", 1, 1),
                     varray_alias("x", ["x1", "x2", "x3"]), var("i", 3, 3)],
                    ["gecode_minimum_arg_int_offset(x, 1, i)"]),
              ["x1", "x2", "x3", "x", "i"],
              {"x1": [3], "x2": [1], "x3": [1], "x": [[3, 1, 1]], "i": [3]},
              lambda a: a["i"] == min(k for k in range(3) if a["x"][k] == min(a["x"])) + 1))
    # sort pin: x=[3,1,2] -> y=[1,2,3]
    P.append(("pin_sort",
              model([var("x1", 3, 3), var("x2", 1, 1), var("x3", 2, 2),
                     varray_alias("x", ["x1", "x2", "x3"]),
                     varray("y", 3, 1, 3)],
                    ["fzn_sort(x, y)"]),
              ["x1", "x2", "x3", "x", "y"],
              {"x1": [3], "x2": [1], "x3": [2], "x": [[3, 1, 2]], "y": arr_domain(3, 1, 3)},
              lambda a: a["y"] == sorted(a["x"])))
    # among_seq pin: every window of 2 has exactly two 1s -> x=[1,1,1]
    P.append(("pin_among",
              model([varray("x", 3, 0, 1)], ["gecode_among_seq_int(x, 1..1, 2, 2, 2)"]),
              ["x"], {"x": arr_domain(3, 0, 1)},
              lambda a: all(sum(a["x"][k:k + 2]) == 2 for k in range(2))))
    # element2d pin: 2x2, i=2, j=2 -> v = a[4]
    P.append(("pin_elem2d",
              model([varray("a", 4, 1, 3), var("i", 2, 2), var("j", 2, 2), var("v", 1, 3)],
                    ["gecode_int_element2d(i, j, a, 1..2, 1..2, v)"]),
              ["a", "i", "j", "v"],
              {"a": arr_domain(4, 1, 3), "i": [2], "j": [2], "v": [1, 2, 3]},
              lambda a: a["v"] == a["a"][(a["i"] - 1) * 2 + (a["j"] - 1)]))
    return P


# ------------------------------------------------------- minizinc e2e part
E2E_MODELS = [
    ("e2e_at_most", 'include "globals.mzn";\narray[1..5] of var 0..1: x :: output_array([1..5]);\nconstraint at_most(2, x, 1);\nconstraint sum(x) >= 3;\nsolve satisfy;\n'),
    ("e2e_at_least", 'include "globals.mzn";\narray[1..5] of var 0..1: x :: output_array([1..5]);\nconstraint at_least(3, x, 1);\nconstraint sum(x) <= 4;\nsolve satisfy;\n'),
    ("e2e_precede", 'include "globals.mzn";\narray[1..6] of var 1..3: x :: output_array([1..6]);\nconstraint value_precede(1, 3, x);\nsolve satisfy;\n'),
    ("e2e_precede_chain", 'include "globals.mzn";\narray[1..6] of var 1..3: x :: output_array([1..6]);\nconstraint value_precede_chain([1, 2, 3], x);\nsolve satisfy;\n'),
    ("e2e_argmin", 'include "globals.mzn";\narray[1..5] of var 1..4: x :: output_array([1..5]);\nvar 1..5: i :: output_var;\nconstraint minimum_arg(x, i);\nsolve satisfy;\n'),
    ("e2e_argmax", 'include "globals.mzn";\narray[1..5] of var 1..4: x :: output_array([1..5]);\nvar 1..5: i :: output_var;\nconstraint maximum_arg(x, i);\nsolve satisfy;\n'),
    ("e2e_sort", 'include "globals.mzn";\narray[1..4] of var 1..4: x :: output_array([1..4]);\narray[1..4] of var 1..4: y :: output_array([1..4]);\nconstraint sort(x, y);\nsolve satisfy;\n'),
    ("e2e_arg_sort", 'include "globals.mzn";\narray[1..4] of var 1..4: x :: output_array([1..4]);\narray[1..4] of var 1..4: p :: output_array([1..4]);\nconstraint arg_sort(x, p);\nsolve satisfy;\n'),
    ("e2e_sliding", 'include "globals.mzn";\narray[1..7] of var 1..3: x :: output_array([1..7]);\nconstraint sliding_among(1, 2, 3, x, {2});\nsolve satisfy;\n'),
    ("e2e_sliding_bool", 'include "globals.mzn";\narray[1..7] of var bool: x :: output_array([1..7]);\nconstraint sliding_among(2, 3, 4, x, true);\nsolve satisfy;\n'),
    ("e2e_element2d", 'include "globals.mzn";\narray[1..2, 1..2] of var 1..3: a :: output_array([1..2, 1..2]);\nvar 1..2: i :: output_var;\nvar 1..2: j :: output_var;\nvar 1..3: v :: output_var;\nconstraint v = a[i, j];\nsolve satisfy;\n'),
    ("e2e_regular2d", 'include "globals.mzn";\narray[1..5] of var 0..1: x :: output_array([1..5]);\narray[1..2, 0..1] of opt int: d = array2d(1..2, 0..1, [1, 2, 2, 1]);\nconstraint regular(x, d, 1, {1,2});\nsolve satisfy;\n'),
]


def e2e_section(tmp):
    if not (os.path.exists(MINIZINC) and os.path.exists(FZN_GECODE)):
        print("e2e: minizinc/fzn-gecode not available, skipped")
        return 0, 0
    ok = fail = 0
    for name, src in E2E_MODELS:
        mp = os.path.join(tmp, name + ".mzn")
        fp = os.path.join(tmp, name + ".fzn")
        open(mp, "w").write(src)
        c = run([MINIZINC, "-c", "--solver", "gecode", mp, "--fzn", fp], 120)
        if c.returncode != 0:
            print(f"[{name}] COMPILE FAIL: {c.stderr.strip()[:100]}")
            fail += 1
            continue
        try:
            mine_all = run([FZ, "-a", fp], 90).stdout
            ref_all = run([FZN_GECODE, "-a", fp], 90).stdout
        except subprocess.TimeoutExpired:
            print(f"[{name}] timeout -> FAIL")
            fail += 1
            continue
        ms, rs = status_of(mine_all), status_of(ref_all)
        if ms == "UNKNOWN":
            print(f"[{name}] MINE=UNKNOWN -> FAIL (must be handled)")
            fail += 1
            continue
        if ms != rs:
            print(f"[{name}] mine={ms} ref={rs} -> FAIL")
            fail += 1
            continue
        if ms == "UNSAT":
            ok += 1
            continue
        def bkey(b):
            return tuple(sorted((k, tuple(v) if isinstance(v, list) else v)
                                for k, v in b.items()))
        mset = {bkey(b) for b in parse_blocks(mine_all)}
        rset = {bkey(b) for b in parse_blocks(ref_all)}
        if mset == rset:
            ok += 1
        else:
            print(f"[{name}] solution sets differ: mine={len(mset)} ref={len(rset)} "
                  f"(mine-only={len(mset - rset)}, ref-only={len(rset - mset)}) -> FAIL")
            fail += 1
    return ok, fail


# ------------------------------------------------------------------- main
def check_instance(inst, label):
    """Returns (ok, wrong, unknown)."""
    total = 1
    for nm in inst.names:
        total *= len(inst.domains[nm])
    if total > 200000:
        return 0, 0, 0
    exp = inst.brute()
    want = {inst.key(a) for a in exp}
    try:
        out_all = run_fzn_text(inst.text, "-a", timeout=90)
    except subprocess.TimeoutExpired:
        print(f"[{label}] -a timeout -> WRONG")
        return 0, 1, 0
    st = status_of(out_all)
    if st == "UNKNOWN":
        print(f"[{label}] UNKNOWN -> FAIL (handled pred)")
        return 0, 0, 1
    got = {inst.key(a) for a in parse_blocks(out_all)}
    if got != want:
        print(f"[{label}] -a mismatch: got {len(got)} want {len(want)} "
              f"miss={len(want - got)} extra={len(got - want)} -> WRONG")
        return 0, 1, 0
    try:
        out1 = run_fzn_text(inst.text, timeout=90)
    except subprocess.TimeoutExpired:
        print(f"[{label}] single-run timeout -> WRONG")
        return 0, 1, 0
    st1 = status_of(out1)
    if want:
        if st1 != "SAT":
            print(f"[{label}] single-run {st1} but witnesses exist -> WRONG")
            return 0, 1, 0
        a1 = parse_blocks(out1)
        if not a1 or inst.key(a1[0]) not in want:
            print(f"[{label}] single-run assignment not a witness -> WRONG")
            return 0, 1, 0
    else:
        if st1 != "UNSAT":
            print(f"[{label}] single-run {st1} but no witnesses -> WRONG")
            return 0, 1, 0
    return 1, 0, 0


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261102
    rng = random.Random(seed)
    ok = wrong = unknown = 0
    tmp = tempfile.mkdtemp(prefix="fznglob")
    try:
        for label, text, names, doms, check in pins():
            o, w, u = check_instance(Inst(text, names, doms, check), label)
            ok += o; wrong += w; unknown += u
        for gi, gen in enumerate(GENS):
            for k in range(n):
                inst = gen(rng)
                o, w, u = check_instance(inst, f"gen{gi}.{k}")
                ok += o; wrong += w; unknown += u
        # optimize spot-check: maximize sum(y) over fzn_sort
        for k in range(max(4, n // 4)):
            inst = gen_sort(rng)
            total = 1
            for nm in inst.names:
                total *= len(inst.domains[nm])
            if total > 100000:
                continue
            exp = inst.brute()
            if not exp:
                continue
            opt = max(sum(a["y"]) for a in exp)
            ln = len(inst.domains["x"][0])
            objexpr = " + ".join(f"y[{i + 1}]" for i in range(ln))
            text = inst.text.replace("solve satisfy;", f"solve maximize {objexpr};")
            try:
                out = run_fzn_text(text, timeout=90)
            except subprocess.TimeoutExpired:
                print(f"[opt.{k}] timeout -> WRONG")
                wrong += 1
                continue
            st = status_of(out)
            blocks = parse_blocks(out)
            obj = sum(blocks[-1]["y"]) if (st == "SAT" and blocks) else None
            if st == "UNKNOWN" or obj is None or abs(obj - opt) > 1e-6:
                print(f"[opt.{k}] sort-opt st={st} obj={obj} want={opt} -> WRONG")
                wrong += 1
            else:
                ok += 1
        eok, efail = e2e_section(tmp)
        ok += eok
        wrong += efail
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"fzn_globals_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown} (seed {seed}, N={n})")
    return 0 if wrong == 0 and unknown == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
