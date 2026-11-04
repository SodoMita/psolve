#!/usr/bin/env python3
"""Differential verifier for the cp-sat (ortools_*) parity batch in
src/fzn.c (handle_constraint):

  ortools_circuit / ortools_subcircuit(x, base)
  ortools_inverse(f, invf, base_f, base_invf)
  ortools_count_eq / ortools_count_eq_cst(x, y, c)
  ortools_global_cardinality(x, cover, counts, closed)
  ortools_global_cardinality_low_up(x, cover, l, u, closed)
  ortools_lex_less_int/bool, ortools_lex_lesseq_int/bool(x, y)
  ortools_nvalue(n, x)
  ortools_precede_chain_int(T, X)
  ortools_arg_max_int/bool(x, z, min_index, multiplier)
  ortools_table_int / ortools_table_bool(x, flat)
  ortools_array_{int,var_int,bool,var_bool}_element(idx, idxset, x, c)
  ortools_bin_packing(c, bin, w) / _capa(c_arr, bin, c_set, w) /
      _load(load, bin, load_set, w)
  fzn_cumulative(s, d, r, b) with VAR capacity
  ortools_cumulative_opt(o, s, d, r, b)
  ortools_disjunctive_strict_opt(o, s, d)
  ortools_network_flow(arc, bal, base, flow) / _cost(..., weight, cost)
  ortools_regular(x, Q, S, d_flat, q0, F)
  fzn_diffn(x, y, dx, dy)

Two independent oracles per random instance (direct .fzn, all vars output):
  1. brute force over declared domains with MiniZinc/cp-sat semantics in
     Python; solution SETS must match exactly
  2. the bundled fzn-cp-sat oracle with -a (verdicts and -a solution sets)

Verdict contract:
  - these preds are now handled: UNKNOWN is a FAILURE, not honesty
  - UNSATISFIABLE must agree with both oracles (proven)
  - SAT solution sets must match both oracles exactly (verified)

Usage: fzn_cpsat_verify.py [N] [seed]     (N random instances per family)
Env: FZNSOLVE, FZN_CP_SAT overridable.
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

FZ = os.environ.get("FZNSOLVE", "./fznsolve")
FZN_CP_SAT = os.environ.get("FZN_CP_SAT", "/home/user/mz/bin/fzn-cp-sat")

PRED_DECLS = """
predicate ortools_circuit(array [int] of var int: x, int: base);
predicate ortools_subcircuit(array [int] of var int: x, int: base);
predicate ortools_inverse(array [int] of var int: f, array [int] of var int: invf, int: base_f, int: base_invf);
predicate ortools_count_eq(array [int] of var int: x, var int: y, var int: c);
predicate ortools_count_eq_cst(array [int] of var int: x, int: ycst, var int: c);
predicate ortools_global_cardinality(array [int] of var int: x, array [int] of int: cover, array [int] of var int: counts, bool: closed);
predicate ortools_global_cardinality_low_up(array [int] of var int: x, array [int] of int: cover, array [int] of var int: lbound, array [int] of var int: ubound, bool: closed);
predicate ortools_lex_less_int(array [int] of var int: x, array [int] of var int: y);
predicate ortools_lex_lesseq_int(array [int] of var int: x, array [int] of var int: y);
predicate ortools_lex_less_bool(array [int] of var bool: x, array [int] of var bool: y);
predicate ortools_lex_lesseq_bool(array [int] of var bool: x, array [int] of var bool: y);
predicate ortools_nvalue(var int: n, array [int] of var int: x);
predicate ortools_precede_chain_int(array [int] of int: T, array [int] of var int: X);
predicate ortools_arg_max_int(array [int] of var int: x, var int: z, int: min_index, int: multiplier);
predicate ortools_arg_max_bool(array [int] of var bool: x, var bool: z, int: min_index, int: multiplier);
predicate ortools_table_int(array [int] of var int: x, array [int] of int: t);
predicate ortools_table_bool(array [int] of var bool: x, array [int] of int: t);
predicate ortools_array_int_element(var int: idx, set of int: index_set_of_x, array [int] of int: x, var int: c);
predicate ortools_array_var_int_element(var int: idx, set of int: index_set_of_x, array [int] of var int: x, var int: c);
predicate ortools_array_bool_element(var int: idx, set of int: index_set_of_x, array [int] of bool: x, var bool: c);
predicate ortools_array_var_bool_element(var int: idx, set of int: index_set_of_x, array [int] of var bool: x, var bool: c);
predicate ortools_bin_packing(int: c, array [int] of var int: bin, array [int] of int: w);
predicate ortools_bin_packing_capa(array [int] of int: c, array [int] of var int: bin, set of int: c_set, array [int] of int: w);
predicate ortools_bin_packing_load(array [int] of var int: load, array [int] of var int: bin, set of int: load_set, array [int] of int: w);
predicate fzn_cumulative(array [int] of var int: s, array [int] of var int: d, array [int] of var int: r, var int: b);
predicate ortools_cumulative_opt(array [int] of var bool: o, array [int] of var int: s, array [int] of var int: d, array [int] of var int: r, var int: b);
predicate ortools_disjunctive_strict_opt(array [int] of var bool: o, array [int] of var int: s, array [int] of var int: d);
predicate ortools_network_flow(array [int] of int: arc, array [int] of int: balance, int: base_node, array [int] of var int: flow);
predicate ortools_network_flow_cost(array [int] of int: arc, array [int] of int: balance, int: base_node, array [int] of var int: flow, array [int] of int: weight, var int: cost);
predicate ortools_regular(array [int] of var int: x, int: Q, int: S, array [int] of int: d, int: q0, set of int: F);
predicate fzn_diffn(array [int] of var int: x, array [int] of var int: y, array [int] of var int: dx, array [int] of var int: dy);
"""


def run(cmd, timeout=90):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def run_fzn_text(text, solver, *options, timeout=90):
    fd, path = tempfile.mkstemp(suffix=".fzn")
    try:
        os.write(fd, text.encode())
        os.close(fd)
        p = run([solver, *options, path], timeout)
        return p.stdout
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        os.unlink(path)


def run_fzn(text, *options, timeout=90):
    return run_fzn_text(text, FZ, *options, timeout=timeout)


def run_oracle(text, *options, timeout=90):
    return run_fzn_text(text, FZN_CP_SAT, *options, timeout=timeout)


# ---------------------------------------------------------------- fzn text
def var(name, lo, hi):
    return f"var {lo}..{hi}: {name} :: output_var;"


def varray(name, n, lo, hi):
    return (f"array[1..{n}] of var {lo}..{hi}: {name}"
            f" :: output_array([1..{n}]);")


def parry(name, vals):
    return f"array[1..{len(vals)}] of int: {name} = [{','.join(str(v) for v in vals)}];"


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

    def key_set(self, sols):
        return set(self.key(a) for a in sols)


def dom(n, lo, hi):
    return [list(c) for c in itertools.product(range(lo, hi + 1), repeat=n)]


def dom1(lo, hi):
    return list(range(lo, hi + 1))


# ------------------------------------------------------------ generators
def gen_circuit(rng, sub=False):
    n = rng.randint(2, 4)
    base = 1
    decls = [varray("x", n, base, base + n - 1)]
    pred = "ortools_subcircuit" if sub else "ortools_circuit"
    cons = [f"{pred}(x, {base})"]
    pin = None
    if rng.random() < 0.5 and n >= 3:
        i = rng.randint(1, n)
        pin = (i, base + (i % n))
        cons.append(f"int_eq(x[{i}], {pin[1]})")

    def check(a):
        x = a["x"]
        if pin and x[pin[0] - 1] != pin[1]:
            return False
        if sub:
            nxt = [v for i, v in enumerate(x) if v != base + i]
            if len(nxt) != len(set(nxt)):
                return False
            if not nxt:
                return True
            seen = set()
            cur = nxt[0]
            while cur not in seen:
                seen.add(cur)
                i = cur - base
                cur = x[i]
                if cur == base + i:
                    return False
            return len(seen) == len(nxt)
        if sorted(x) != list(range(base, base + n)):
            return False
        seen = set()
        cur = base
        while cur not in seen:
            seen.add(cur)
            cur = x[cur - base]
        return len(seen) == n and cur == base
    return Inst(model(decls, cons), ["x"], {"x": dom(n, base, base + n - 1)}, check)


def gen_inverse(rng):
    n = rng.randint(2, 4)
    decls = [varray("f", n, 1, n), varray("inv", n, 1, n)]
    cons = ["ortools_inverse(f, inv, 1, 1)"]

    def check(a):
        f, inv = a["f"], a["inv"]
        for i in range(n):
            if not (1 <= f[i] <= n) or inv[f[i] - 1] != i + 1:
                return False
        for j in range(n):
            if not (1 <= inv[j] <= n) or f[inv[j] - 1] != j + 1:
                return False
        return True
    return Inst(model(decls, cons), ["f", "inv"], {"f": dom(n, 1, n), "inv": dom(n, 1, n)}, check)


def gen_count(rng):
    n = rng.randint(2, 4)
    lo, hi = 1, 3
    cst = rng.random() < 0.5
    decls = [varray("x", n, lo, hi), var("c", 0, n)]
    names = ["x", "c"]
    domains = {"x": dom(n, lo, hi), "c": dom1(0, n)}
    if cst:
        y = rng.randint(lo, hi)
        cons = [f"ortools_count_eq_cst(x, {y}, c)"]

        def check(a):
            return a["c"] == sum(1 for v in a["x"] if v == y)
    else:
        decls.append(var("y", lo, hi))
        cons = ["ortools_count_eq(x, y, c)"]
        names.append("y")
        domains["y"] = dom1(lo, hi)

        def check(a):
            return a["c"] == sum(1 for v in a["x"] if v == a["y"])
    return Inst(model(decls, cons), names, domains, check)


def gen_gc(rng):
    n = rng.randint(2, 5)
    lo, hi = 1, 3
    cover = sorted(rng.sample(range(lo, hi + 1), rng.randint(2, 3)))
    nc = len(cover)
    lowup = rng.random() < 0.5
    closed = rng.random() < 0.5
    decls = [varray("x", n, lo, hi), parry("cover", cover)]
    cb = "true" if closed else "false"
    if lowup:
        l = [rng.randint(0, 2) for _ in range(nc)]
        u = [rng.randint(l[k], n) for k in range(nc)]
        decls += [parry("l", l), parry("u", u)]
        cons = [f"ortools_global_cardinality_low_up(x, cover, l, u, {cb})"]

        def check(a):
            x = a["x"]
            if closed and any(v not in cover for v in x):
                return False
            return all(l[k] <= sum(1 for v in x if v == cover[k]) <= u[k]
                       for k in range(nc))
        names = ["x"]
        domains = {"x": dom(n, lo, hi)}
    else:
        counts = [rng.randint(0, n) for _ in range(nc)]
        decls.append(parry("cnt", counts))
        cons = [f"ortools_global_cardinality(x, cover, cnt, {cb})"]

        def check(a):
            x = a["x"]
            if closed and any(v not in cover for v in x):
                return False
            return all(counts[k] == sum(1 for v in x if v == cover[k]) for k in range(nc))
        names = ["x"]
        domains = {"x": dom(n, lo, hi)}
    return Inst(model(decls, cons), names, domains, check)


def gen_lex(rng):
    n = rng.randint(2, 4)
    isbool = rng.random() < 0.5
    le = rng.random() < 0.5
    lo, hi = (0, 1) if isbool else (1, 3)
    t = "bool" if isbool else "int"
    pred = f"ortools_lex_lesseq_{t}" if le else f"ortools_lex_less_{t}"
    decls = [varray("x", n, lo, hi), varray("y", n, lo, hi)]
    cons = [f"{pred}(x, y)"]

    def check(a):
        x, y = a["x"], a["y"]
        return x <= y if le else x < y
    return Inst(model(decls, cons), ["x", "y"], {"x": dom(n, lo, hi), "y": dom(n, lo, hi)}, check)


def gen_nvalue(rng):
    n = rng.randint(2, 5)
    lo, hi = 1, 3
    decls = [var("n", 0, n), varray("x", n, lo, hi)]
    cons = ["ortools_nvalue(n, x)"]

    def check(a):
        return a["n"] == len(set(a["x"]))
    return Inst(model(decls, cons), ["n", "x"], {"n": dom1(0, n), "x": dom(n, lo, hi)}, check)


def gen_precede(rng):
    n = rng.randint(3, 5)
    m = rng.randint(2, 3)
    lo, hi = 1, 4
    T = [rng.randint(lo, hi) for _ in range(m)]
    decls = [parry("T", T), varray("X", n, lo, hi)]
    cons = ["ortools_precede_chain_int(T, X)"]

    def check(a):
        X = a["X"]
        pos = {}
        for i, v in enumerate(X):
            if v not in pos:
                pos[v] = i
        x = a["X"]
        for k in range(len(T) - 1):
            s, t = T[k], T[k + 1]
            for j in range(len(x)):
                if x[j] == t and not any(x[i] == s for i in range(j)):
                    return False
        return True
    return Inst(model(decls, cons), ["X"], {"X": dom(n, lo, hi)}, check)


def gen_argmax(rng):
    n = rng.randint(2, 4)
    isbool = rng.random() < 0.3
    lo, hi = (0, 1) if isbool else (1, 4)
    mi = 1
    m = rng.choice([1, -1])
    t = "bool" if isbool else "int"
    decls = [varray("x", n, lo, hi), var("z", mi, mi + n - 1)]
    cons = [f"ortools_arg_max_{t}(x, z, {mi}, {m})"]

    def check(a):
        x = a["x"]
        ext = max(x) if m > 0 else min(x)
        pos0 = next(i for i in range(n) if x[i] == ext)
        return a["z"] == mi + abs(m) * pos0
    return Inst(model(decls, cons), ["x", "z"], {"x": dom(n, lo, hi), "z": dom1(mi, mi + n - 1)}, check)


def gen_table(rng):
    n = rng.randint(2, 3)
    isbool = rng.random() < 0.5
    lo, hi = (0, 1) if isbool else (1, 3)
    rows = rng.randint(1, 3)
    t = "bool" if isbool else "int"
    flat = [rng.randint(lo, hi) for _ in range(rows * n)]
    decls = [varray("x", n, lo, hi), parry("t", flat)]
    cons = [f"ortools_table_{t}(x, t)"]

    def check(a):
        x = tuple(a["x"])
        return any(tuple(flat[r * n:(r + 1) * n]) == x for r in range(rows))
    return Inst(model(decls, cons), ["x"], {"x": dom(n, lo, hi)}, check)


def gen_element(rng):
    n = rng.randint(2, 4)
    isbool = rng.random() < 0.5
    lo, hi = (0, 1) if isbool else (1, 4)
    if rng.random() < 0.5:
        ilo, ihi = 1, n
        idxset = f"{ilo}..{ihi}"
        idom = dom1(ilo, ihi)
        setmin = ilo
    else:
        vals = sorted(rng.sample(range(1, n + 1), rng.randint(1, n)))
        idxset = "{" + ",".join(str(v) for v in vals) + "}"
        setmin = vals[0]
        # cp-sat maps pos = i - setmin + 1 and only requires 1 <= pos <= n
        # (membership in the enumerated set is not enforced)
        idom = [i for i in range(1, n + 1) if 1 <= i - setmin + 1 <= n]
    pos_of = lambda i: i - setmin
    vt = "bool" if isbool else "int"
    decls = [varray("x", n, lo, hi), var("i", 1, n), var("c", lo, hi)]
    cons = [f"ortools_array_var_{vt}_element(i, {idxset}, x, c)"]

    def check(a):
        return a["c"] == a["x"][pos_of(a["i"])]
    return Inst(model(decls, cons), ["x", "i", "c"],
                {"x": dom(n, lo, hi), "i": idom, "c": dom1(lo, hi)}, check)


def gen_bin_packing(rng):
    n = rng.randint(2, 4)
    kind = rng.choice(["plain", "capa", "load"])
    nb = rng.randint(1, 3)
    w = [rng.randint(1, 3) for _ in range(n)]
    decls = [varray("bin", n, 1, nb), parry("w", w)]
    names = ["bin"]
    domains = {"bin": dom(n, 1, nb)}
    if kind == "plain":
        c = rng.randint(2, 6)
        cons = [f"ortools_bin_packing({c}, bin, w)"]

        def check(a):
            b = a["bin"]
            return all(sum(w[i] for i in range(n) if b[i] == j) <= c for j in range(1, nb + 1))
    elif kind == "capa":
        cvals = [rng.randint(1, 6) for _ in range(nb)]
        cset = list(range(1, nb + 1))
        decls.append(parry("capa", cvals))
        cons = [f"ortools_bin_packing_capa(capa, bin, 1..{nb}, w)"]

        def check(a):
            b = a["bin"]
            if any(v not in cset for v in b):
                return False
            return all(sum(w[i] for i in range(n) if b[i] == j) <= cvals[j - 1]
                       for j in cset)
    else:
        decls.append(varray("load", nb, 0, 8))
        cons = [f"ortools_bin_packing_load(load, bin, 1..{nb}, w)"]
        names.append("load")
        domains["load"] = dom(nb, 0, 8)

        def check(a):
            b = a["bin"]
            if any(v not in range(1, nb + 1) for v in b):
                return False
            return all(a["load"][j - 1] == sum(w[i] for i in range(n) if b[i] == j)
                       for j in range(1, nb + 1))
    return Inst(model(decls, cons), names, domains, check)


def gen_cumulative(rng, opt=False):
    n = rng.randint(2, 3)
    d = [rng.randint(0, 2) for _ in range(n)]
    r = [rng.randint(0, 2) for _ in range(n)]
    decls = [varray("s", n, 0, 3), parry("d", d), parry("r", r), var("b", 1, 2)]
    names = ["s", "b"]
    domains = {"s": dom(n, 0, 3), "b": dom1(1, 2)}
    if opt:
        decls.append(varray("o", n, 0, 1))
        cons = ["ortools_cumulative_opt(o, s, d, r, b)"]
        names.append("o")
        domains["o"] = dom(n, 0, 1)

        def check(a):
            s, b, o = a["s"], a["b"], a["o"]
            for t in range(0, 6):
                if sum(r[i] for i in range(n)
                       if o[i] == 1 and d[i] > 0 and s[i] <= t < s[i] + d[i]) > b:
                    return False
            return True
    else:
        cons = ["fzn_cumulative(s, d, r, b)"]

        def check(a):
            s, b = a["s"], a["b"]
            for t in range(0, 6):
                if sum(r[i] for i in range(n)
                       if d[i] > 0 and s[i] <= t < s[i] + d[i]) > b:
                    return False
            return True
    return Inst(model(decls, cons), names, domains, check)


def gen_disjunctive(rng):
    n = rng.randint(2, 3)
    decls = [varray("o", n, 0, 1), varray("s", n, 0, 4), parry("d", [rng.randint(0, 2) for _ in range(n)])]
    cons = ["ortools_disjunctive_strict_opt(o, s, d)"]
    d = None

    def check(a):
        o, s = a["o"], a["s"]
        dd = a["d"] if "d" in a else None
        return True
    # d is a par array in the model text; recover values from the generator
    # closure instead:
    dvals = decls[2].split("= [")[1].rstrip("];").split(",")
    dvals = [int(v) for v in dvals]

    def check2(a):
        o, s = a["o"], a["s"]
        for i in range(n):
            for j in range(i + 1, n):
                if o[i] == 1 and o[j] == 1:
                    if not (s[i] + dvals[i] <= s[j] or s[j] + dvals[j] <= s[i]):
                        return False
        return True
    return Inst(model(decls, cons), ["o", "s"], {"o": dom(n, 0, 1), "s": dom(n, 0, 4)}, check2)


def gen_network_flow(rng, cost=False):
    E = rng.randint(2, 4)
    nn = rng.randint(2, 4)
    base = 1
    arcs = []
    for _ in range(E):
        f = rng.randint(base, base + nn - 1)
        t = rng.randint(base, base + nn - 1)
        while t == f:
            t = rng.randint(base, base + nn - 1)
        arcs += [f, t]
    bal = [rng.randint(-2, 2) for _ in range(nn)]
    if sum(bal) != 0:
        bal[0] -= sum(bal)
    decls = [parry("arc", arcs), parry("bal", bal), varray("f", E, 0, 3)]
    names = ["f"]
    domains = {"f": dom(E, 0, 3)}
    if cost:
        w = [rng.randint(1, 4) for _ in range(E)]
        decls += [parry("w", w), var("c", 0, 30)]
        cons = [f"ortools_network_flow_cost(arc, bal, {base}, f, w, c)"]
        names.append("c")
        domains["c"] = dom1(0, 30)

        def check(a):
            f = a["f"]
            for v in range(base, base + nn):
                out = sum(f[k] for k in range(E) if arcs[2 * k] == v)
                inn = sum(f[k] for k in range(E) if arcs[2 * k + 1] == v)
                if out - inn != bal[v - base]:
                    return False
            return a["c"] == sum(w[k] * f[k] for k in range(E))
    else:
        cons = [f"ortools_network_flow(arc, bal, {base}, f)"]

        def check(a):
            f = a["f"]
            for v in range(base, base + nn):
                out = sum(f[k] for k in range(E) if arcs[2 * k] == v)
                inn = sum(f[k] for k in range(E) if arcs[2 * k + 1] == v)
                if out - inn != bal[v - base]:
                    return False
            return True
    return Inst(model(decls, cons), names, domains, check)


def gen_regular(rng):
    n = rng.randint(1, 5)
    Q = rng.randint(1, 3)
    S = rng.randint(1, 2)
    d = [rng.choice([0] + list(range(1, Q + 1))) for _ in range(Q * S)]
    q0 = rng.randint(1, Q)
    F = sorted(rng.sample(range(1, Q + 1), rng.randint(1, Q)))
    decls = [varray("x", n, 1, S), parry("d", d)]
    cons = [f"ortools_regular(x, {Q}, {S}, d, {q0}, {{{','.join(map(str, F))}}})"]

    def check(a):
        x = a["x"]
        q = q0
        for t in range(n):
            s = x[t]
            if not (1 <= s <= S):
                return False
            q = d[(q - 1) * S + (s - 1)]
            if q == 0:
                return False
        return q in F
    return Inst(model(decls, cons), ["x"], {"x": dom(n, 1, S)}, check)


def gen_diffn(rng):
    n = rng.randint(2, 3)
    dx = [rng.randint(1, 3) for _ in range(n)]
    dy = [rng.randint(1, 3) for _ in range(n)]
    decls = [varray("x", n, 0, 3), varray("y", n, 0, 3), parry("dx", dx), parry("dy", dy)]
    cons = ["fzn_diffn(x, y, dx, dy)"]

    def check(a):
        x, y = a["x"], a["y"]
        for i in range(n):
            for j in range(i + 1, n):
                if x[i] < x[j] + dx[j] and x[j] < x[i] + dx[i] and \
                   y[i] < y[j] + dy[j] and y[j] < y[i] + dy[i]:
                    return False
        return True
    return Inst(model(decls, cons), ["x", "y"], {"x": dom(n, 0, 3), "y": dom(n, 0, 3)}, check)


GENERATORS = {
    "circuit": lambda rng: gen_circuit(rng, False),
    "subcircuit": lambda rng: gen_circuit(rng, True),
    "inverse": gen_inverse,
    "count": gen_count,
    "gc": gen_gc,
    "lex": gen_lex,
    "nvalue": gen_nvalue,
    "precede": gen_precede,
    "argmax": gen_argmax,
    "table": gen_table,
    "element": gen_element,
    "binpacking": gen_bin_packing,
    "cumulative": lambda rng: gen_cumulative(rng, False),
    "cumulative_opt": lambda rng: gen_cumulative(rng, True),
    "disjunctive_opt": gen_disjunctive,
    "network_flow": lambda rng: gen_network_flow(rng, False),
    "network_flow_cost": lambda rng: gen_network_flow(rng, True),
    "regular": gen_regular,
    "diffn": gen_diffn,
}


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261104
    rng = random.Random(seed)
    ok = wrong = unknown = 0
    for name, gen in GENERATORS.items():
        for k in range(n):
            inst = gen(rng)
            total = 1
            for nm in inst.names:
                total *= len(inst.domains[nm])
            if total > 200000:
                continue
            exp = inst.key_set(inst.brute())
            try:
                out_f = run_fzn(inst.text, "-a", timeout=90)
                out_c = run_oracle(inst.text, "-a", timeout=90)
            except subprocess.TimeoutExpired:
                print(f"[{name}.{k}] timeout -> WRONG")
                wrong += 1
                continue
            st_f, st_c = status_of(out_f), status_of(out_c)
            got_f = set()
            for blk in parse_blocks(out_f):
                got_f.add(inst.key(blk))
            got_c = set()
            for blk in parse_blocks(out_c):
                got_c.add(inst.key(blk))
            exp_sat = len(exp) > 0
            if st_f == "UNKNOWN":
                print(f"[{name}.{k}] fznsolve UNKNOWN -> FAILURE")
                unknown += 1
                continue
            if (st_f == "SAT") != exp_sat or (st_c == "SAT") != exp_sat:
                print(f"[{name}.{k}] verdict f={st_f} c={st_c} brute_sat={exp_sat} -> WRONG")
                wrong += 1
                continue
            if st_f == "UNSAT" and st_c == "UNSAT" and not exp:
                ok += 1
                continue
            if got_f != exp or got_c != exp:
                print(f"[{name}.{k}] set mismatch: |F|={len(got_f)} |C|={len(got_c)} |brute|={len(exp)}"
                      f" F==brute:{got_f == exp} C==brute:{got_c == exp} -> WRONG")
                wrong += 1
                continue
            ok += 1
    print(f"fzn_cpsat_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown} (seed {seed}, N={n})")
    return 0 if wrong == 0 and unknown == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
