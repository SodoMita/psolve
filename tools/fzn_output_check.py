#!/usr/bin/env python3
"""FlatZinc OUTPUT-LAYER verification (roadmap 6.7): re-parse every byte of
fznsolve output and check it against the model, with an independent exact
oracle.

The input side of the FlatZinc bridge is heavily fuzzed (crash robustness,
unit semantics); the OUTPUT side was only spot-tested.  An output-layer lie
is as dangerous as a solver lie: MiniZinc drivers interpret the completion
markers (`==========` = search complete, its absence = incomplete) and take
the printed assignments on faith; nothing re-checks them.

This tool generates structured models (so an exact evaluator can re-check
each printed assignment against EVERY constraint and the objective), runs
fznsolve over satisfy / minimize / maximize, plain and -a, and asserts:

  marker protocol    every solution block ends with '----------'; plain
                     satisfy SAT ends there (NO '=========='); plain
                     optimize SAT ends '----------' then '==========';
                     -a with >=1 solution ends '==========', with none
                     '=====UNSATISFIABLE====='; no other markers;
                     '%mzn-stat: objective=' echo present exactly for
                     plain-optimize-SAT and equal to the printed solution's
                     objective (and, brute-force models: the true optimum);
  solutions          each printed assignment satisfies every constraint and
                     lies inside its declared domain (int: exactly, in
                     Fractions; float: within a scaled LP feasibility
                     tolerance -- decimal round-trips of binary doubles
                     cannot be checked exactly); aliased outputs are
                     consistent with their base var in the same block
                     (incl. constant alias slots);
  -a enumeration     no duplicate output projections; for satisfy the
                     printed projection set EQUALS the brute-forced one;
                     for optimize the objectives improve strictly and the
                     final incumbent IS the brute-force optimum;
  honesty            a printed UNSATISFIABLE on a brute-force-satisfiable
                     model is a fabrication (WRONG); solver declines /
                     UNKNOWN markers are honest and counted separately.

usage: fzn_output_check.py [N] [seed] [jobs]
env:   PSOLVE_FZNSOLVE overrides the binary.
"""
import os, re, subprocess, sys, random
from fractions import Fraction
from itertools import product
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/psolve_fznout"
FZNSOLVE = os.path.abspath(os.environ.get("PSOLVE_FZNSOLVE",
                                          os.path.join(ROOT, "fznsolve")))


class Model:
    def __init__(self):
        self.vars = []      # (name, dom)  dom = ('range',lo,hi) | ('set',[..])
        self.boolish = []   # names with domain exactly 0..1 (reif targets)
        self.cons = []
        self.solve = ("satisfy",)


def gen_float_model(rng):
    """Bounded float variables + float_lin_* / float_in rows, dyadic-rational
    data only.  Checked for marker protocol, printed-point feasibility within
    a scaled LP feasibility tolerance (FLOAT_RTOL, see eval_float_constraint:
    exact rational checks on decimal round-trips of binary doubles are
    unsound), and (via scipy, benign scales) UNSAT honesty and optimum
    agreement -- never brute force."""
    m = Model()
    n = rng.randint(2, 4)
    DYAD = [Fraction(-2), Fraction(-1), Fraction(-1, 2), Fraction(0),
            Fraction(1, 2), Fraction(1), Fraction(3, 2), Fraction(2)]
    for j in range(n):
        lo = Fraction(rng.randint(-4, 1))
        hi = lo + Fraction(rng.randint(1, 4))
        m.vars.append(("f%d" % j, ('frange', lo, hi)))
    names = [v[0] for v in m.vars]
    for _ in range(rng.randint(1, 4)):
        t = rng.random()
        k = rng.randint(1, n)
        sel = rng.sample(names, k)
        cs = [rng.choice(DYAD) for _ in range(k)]
        if any(c != 0 for c in cs):
            if t < 0.5:
                op = 'le'
            elif t < 0.75:
                op = 'eq'
            else:
                op = 'ge'
            d = Fraction(rng.randint(-6, 10))
            m.cons.append(('float_lin_' + op, cs, sel, d))
        else:
            a = rng.choice(names)
            lo = Fraction(rng.randint(-3, 1)); hi = lo + Fraction(rng.randint(1, 3))
            m.cons.append(('float_in_row', a, lo, hi))
    sr = rng.random()
    if sr < 0.5:
        m.solve = ("satisfy",)
    else:
        m.solve = (rng.choice(['min', 'max']), rng.choice(names))
    return m


def gen_model(rng):
    m = Model()
    n = rng.randint(2, 4)
    for j in range(n):
        r = rng.random()
        nm = "x%d" % j
        if r < 0.2:
            dom = ('range', 0, 1)
            m.boolish.append(nm)
        elif r < 0.8:
            lo = rng.randint(-4, 1)
            dom = ('range', lo, lo + rng.randint(1, 4))
        else:
            dom = ('set', sorted(rng.sample(range(-4, 5), rng.randint(2, 4))))
        m.vars.append((nm, dom))
    names = [v[0] for v in m.vars]

    for _ in range(rng.randint(1, 4)):
        t = rng.random()
        k = rng.randint(1, n)
        sel = rng.sample(names, k)
        if t < 0.42:
            cs = [rng.randint(-4, 4) for _ in range(k)]
            op = rng.choice(['le', 'eq', 'ge', 'gt', 'ne'])
            m.cons.append(('int_lin_' + op, cs, sel, rng.randint(-8, 10)))
        elif t < 0.52 and n >= 2:
            m.cons.append(('all_different', sel))
        elif t < 0.62 and m.boolish:
            a, b = rng.sample(names, 2)
            m.cons.append(('reif', rng.choice(['eq', 'ne', 'le', 'lt']),
                           a, b, rng.choice(m.boolish)))
        elif t < 0.72:
            tab = rng.sample(names, rng.randint(1, min(3, n)))
            rows = [tuple(rng.randint(-3, 4) for _ in tab)
                    for _ in range(rng.randint(1, 4))]
            m.cons.append(('table', tab, rows))
        elif t < 0.82:
            a, b = rng.sample(names, 2)
            m.cons.append(('int_abs', a, b))
        elif t < 0.9:
            a, b = rng.sample(names, 2)
            c = rng.choice(names)
            m.cons.append((rng.choice(['int_max', 'int_min', 'int_times']), a, b, c))
        elif t < 0.96:
            m.cons.append(('set_in', rng.choice(names),
                           rng.sample(range(-2, 5), rng.randint(1, 4))))
        else:
            a = rng.choice(names)
            S = sorted(rng.sample(range(-3, 4), rng.randint(1, 3)))
            m.cons.append(('element', a, S, rng.choice(names)))
    sr = rng.random()
    if sr < 0.4:
        m.solve = ("satisfy",)
    else:
        m.solve = (rng.choice(['min', 'max']), rng.choice(names))
    return m


def emit_float_fzn(m, path):
    L = []
    for (nm, dom) in m.vars:
        L.append("var %s..%s: %s :: output_var;" % (float(dom[1]), float(dom[2]), nm))
    for c in m.cons:
        nm = c[0]
        if nm.startswith('float_lin_'):
            op = nm[10:]
            L.append("constraint float_lin_%s([%s], [%s], %s);"
                     % (op, ", ".join(repr(float(v)) for v in c[1]),
                        ", ".join(c[2]), repr(float(c[3]))))
        elif nm == 'float_in_row':
            L.append("constraint float_in(%s, %s, %s);"
                     % (c[1], repr(float(c[2])), repr(float(c[3]))))
        else:
            raise AssertionError(nm)
    if m.solve[0] == 'satisfy':
        L.append("solve satisfy;")
    else:
        L.append("solve %s %s;" % ('minimize' if m.solve[0] == 'min' else 'maximize',
                                   m.solve[1]))
    open(path, "w").write("\n".join(L) + "\n")
    return [{'name': nm, 'is_array': False, 'refs': [('var', nm)], 'index_lo': None}
            for (nm, dom) in m.vars]


def emit_fzn(m, path, use_aliases, rng):
    L = []
    for (nm, dom) in m.vars:
        if dom[0] == 'range':
            L.append("var %d..%d: %s :: output_var;" % (dom[1], dom[2], nm))
        else:
            L.append("var {%s}: %s :: output_var;"
                     % (", ".join(map(str, dom[1])), nm))
    for c in m.cons:
        nm = c[0]
        if nm.startswith('int_lin_'):
            L.append("constraint %s([%s], [%s], %d);"
                     % (nm, ", ".join(map(str, c[1])), ", ".join(c[2]), c[3]))
        elif nm == 'all_different':
            L.append("constraint all_different([%s]);" % ", ".join(c[1]))
        elif nm == 'reif':
            L.append("constraint int_%s_reif(%s, %s, %s);"
                     % (c[1], c[2], c[3], c[4]))
        elif nm == 'table':
            tab, rows = c[1], c[2]
            flat = ", ".join(str(v) for row in rows for v in row)
            L.append("constraint table([%s], array2d(1, %d, 1, %d, [%s]));"
                     % (", ".join(tab), len(rows), len(tab), flat))
        elif nm == 'int_abs':
            L.append("constraint int_abs(%s, %s);" % (c[1], c[2]))
        elif nm in ('int_max', 'int_min', 'int_times'):
            L.append("constraint %s(%s, %s, %s);" % (nm, c[1], c[2], c[3]))
        elif nm == 'set_in':
            L.append("constraint set_in(%s, {%s});"
                     % (c[1], ", ".join(map(str, sorted(set(c[2]))))))
        elif nm == 'element':
            S = ", ".join(map(str, c[2]))
            L.append("constraint array_int_element(%s, [%s], %s);" % (c[1], S, c[3]))
        else:
            raise AssertionError(nm)
    if m.solve[0] == 'satisfy':
        L.append("solve satisfy;")
    else:
        L.append("solve %s %s;" % ('minimize' if m.solve[0] == 'min' else 'maximize',
                                   m.solve[1]))

    decls = [{'name': nm, 'is_array': False, 'refs': [('var', nm)], 'index_lo': None}
             for (nm, dom) in m.vars]
    if use_aliases and rng.random() < 0.6 and len(m.vars) >= 2:
        names = [v[0] for v in m.vars]
        a, b = rng.sample(names, 2)
        L.append("var int: al :: output_var = %s;" % a)
        decls.append({'name': 'al', 'is_array': False, 'refs': [('var', a)],
                      'index_lo': None})
        c7 = rng.choice([7, -3])
        L.append("array [1..3] of var int: vw :: output_array([1..3]) = [%s, %d, %s];"
                 % (b, c7, a))
        decls.append({'name': 'vw', 'is_array': True,
                      'refs': [('var', b), ('const', c7), ('var', a)], 'index_lo': 1})
    open(path, "w").write("\n".join(L) + "\n")
    return decls


# ------------------------------------------------------------- evaluator --
def eval_constraint(c, val):
    nm = c[0]
    if nm.startswith('int_lin_'):
        op = nm[8:]
        s = sum(c[1][i] * val[v] for i, v in enumerate(c[2]))
        d = c[3]
        return {'le': s <= d, 'ge': s >= d, 'eq': s == d,
                'gt': s > d, 'ne': s != d}[op]
    if nm == 'all_different':
        vs = [val[v] for v in c[1]]
        return len(set(vs)) == len(vs)
    if nm == 'reif':
        _, op, a, b, r = c
        base = {'eq': val[a] == val[b], 'ne': val[a] != val[b],
                'le': val[a] <= val[b], 'lt': val[a] < val[b]}[op]
        return base == (val[r] != 0)
    if nm == 'table':
        tab, rows = c[1], c[2]
        tup = tuple(val[v] for v in tab)
        return any(tup == tuple(row) for row in rows)
    if nm == 'int_abs':
        return abs(val[c[1]]) == val[c[2]]
    if nm == 'int_max':
        return max(val[c[1]], val[c[2]]) == val[c[3]]
    if nm == 'int_min':
        return min(val[c[1]], val[c[2]]) == val[c[3]]
    if nm == 'int_times':
        return val[c[1]] * val[c[2]] == val[c[3]]
    if nm == 'set_in':
        return val[c[1]] in set(c[2])
    if nm == 'element':
        i = val[c[1]]
        S = c[2]
        if not (1 <= i <= len(S)):
            return False                      # element failure = violated
        return S[i - 1] == val[c[3]]
    if nm.startswith('float_lin_'):
        op = nm[10:]
        s = sum(c[1][i] * val[v] for i, v in enumerate(c[2]))
        d = c[3]
        return {'le': s <= d, 'ge': s >= d, 'eq': s == d}[op]
    if nm == 'float_in_row':
        return c[2] <= val[c[1]] <= c[3]
    raise AssertionError(nm)


def brute_force(m):
    sols = []
    doms = []
    for (nm, dom) in m.vars:
        doms.append(dom[1] if dom[0] == 'set' else list(range(dom[1], dom[2] + 1)))
    for combo in product(*doms):
        val = {m.vars[i][0]: combo[i] for i in range(len(combo))}
        if all(eval_constraint(c, val) for c in m.cons):
            sols.append(val)
    return sols


def projection(val, decls):
    proj = []
    for d in decls:
        for ref in d['refs']:
            proj.append(val[ref[1]] if ref[0] == 'var' else ref[1])
    return tuple(proj)


# --------------------------------------------------------------- checker --
BLOCK_END, COMPLETE = "----------", "=========="
UNSAT, UNKNOWN = "=====UNSATISFIABLE=====", "=====UNKNOWN====="
STAT_OBJ = re.compile(r"^%+mzn-stat: objective=(\S+)$")

# Hard feasibility tolerance for the float family, scaled per constraint.
# Exact Fraction arithmetic on the printed tokens is NOT a sound check: a
# printed float is a decimal round-trip of a binary double, and LP vertices
# of dyadic-input models are rationals with non-dyadic denominators (e.g.
# x = -4/3 prints as -1.3333333333333333, whose exact Fraction times 3/2 is
# -1.999999999999999995... != -2).  In IEEE double arithmetic the same
# expressions evaluate exactly to the bound.  The solver's own LP path accepts
# points within a feasibility tolerance, as does every production LP solver.
# What must never happen is a *macroscopic* violation (a fabricated point);
# 1e-6 scaled sits ~10 orders above the ~1e-16 round-trip noise observed and
# far below anything a lying solver would print on these O(1)-scale models.
# The largest scaled residual over the run is reported as a drift indicator.
FLOAT_RTOL = Fraction(1, 10**6)


def eval_float_constraint(c, val):
    """(ok, scaled_residual): scaled_residual = violation / (1 + |b| + sum|a_i x_i|)."""
    nm = c[0]
    if nm.startswith('float_lin_'):
        op = nm[10:]
        lhs = sum(c[1][i] * val[v] for i, v in enumerate(c[2]))
        viol = {'le': lhs - c[3], 'ge': c[3] - lhs, 'eq': abs(lhs - c[3])}[op]
        scale = (Fraction(1) + abs(c[3])
                 + sum(abs(c[1][i]) * abs(val[v]) for i, v in enumerate(c[2])))
        r = viol / scale
        return (r <= FLOAT_RTOL), r
    # float_in: c = ('float_in', var, lo, hi)
    v = val[c[1]]
    scale = Fraction(1) + max(abs(c[2]), abs(c[3]))
    r = max(c[2] - v, v - c[3], Fraction(0)) / scale
    return (r <= FLOAT_RTOL), r


def parse_block(blk_lines):
    out = {}
    for ln in blk_lines:
        ln = ln.strip()
        mm = re.match(r"^([A-Za-z_]\w*)\s*=\s*(.*);$", ln)
        if not mm:
            return None, "unparseable output line: %r" % ln
        name, rhs = mm.group(1), mm.group(2)
        am = re.match(r"^array1d\((-?\d+)\.\.(-?\d+),\s*\[(.*)\]\)$", rhs)
        if am:
            toks = [v.strip() for v in am.group(3).split(",")] if am.group(3).strip() else []
            out[name] = ("arr", int(am.group(1)), int(am.group(2)), toks)
        else:
            out[name] = ("scalar", rhs)
    return out, None


def check_block(m, decls, blk):
    amap, err = parse_block(blk)
    if err:
        return None, err
    val = {}
    for (nm, dom) in m.vars:
        e = amap.get(nm)
        if e is None:
            return None, "base var %s not printed" % nm
        if e[0] != "scalar":
            return None, "base var %s printed as array" % nm
        try:
            v = int(e[1])
        except ValueError:
            return None, "bad int token for %s: %r" % (nm, e[1])
        if dom[0] == 'range' and not (dom[1] <= v <= dom[2]):
            return None, "%s=%d outside declared %s" % (nm, v, dom)
        if dom[0] == 'set' and v not in dom[1]:
            return None, "%s=%d outside declared set %s" % (nm, v, dom[1])
        val[nm] = v
    for c in m.cons:
        if not eval_constraint(c, val):
            return None, "constraint %s violated by printed solution" % (c,)
    for d in decls:
        e = amap.get(d['name'])
        if e is None:
            return None, "declared output %s not printed" % d['name']
        toks = e[3] if e[0] == "arr" else [e[1]]
        if len(toks) != len(d['refs']):
            return None, "output %s arity %d, want %d" % (d['name'], len(toks), len(d['refs']))
        if e[0] == "arr" and d['index_lo'] is not None and e[1] != d['index_lo']:
            return None, "output %s index_lo %d, want %d" % (d['name'], e[1], d['index_lo'])
        for tok, ref in zip(toks, d['refs']):
            try:
                got = int(tok)
            except ValueError:
                return None, "output %s bad token %r" % (d['name'], tok)
            want = val[ref[1]] if ref[0] == 'var' else ref[1]
            if got != want:
                return None, ("output %s prints %s for %s, want %s"
                              % (d['name'], tok, ref, want))
    return val, None


def check_model(m, decls, stdout, all_mode):
    sols_bf = brute_force(m)
    sense = m.solve[0]
    objvar = m.solve[1] if sense != 'satisfy' else None
    bf_best = None
    if objvar:
        if not sols_bf:
            bf_best = None
        else:
            o = [s[objvar] for s in sols_bf]
            bf_best = max(o) if sense == 'max' else min(o)

    blocks, markers, stats_obj = [], [], []
    cur = []
    for ln in stdout.splitlines():
        s = ln.strip()
        if s == BLOCK_END:
            blocks.append(cur); cur = []
        elif s in (COMPLETE, UNSAT, UNKNOWN):
            markers.append(s)
        elif STAT_OBJ.match(s):
            stats_obj.append(Fraction(STAT_OBJ.match(s).group(1)))
        elif s.startswith("%"):
            pass
        elif s:
            cur.append(ln)
    if cur:
        return "WRONG", "assignment lines after last marker"

    if len(markers) > 1:
        return "WRONG", "multiple terminal markers: %s" % (markers,)
    terminal = markers[0] if markers else None

    # honesty: printed UNSAT must be exactly true (brute force empty)
    if terminal == UNSAT:
        if blocks:
            return "WRONG", "UNSATISFIABLE printed with solution blocks"
        if sols_bf:
            return "WRONG", ("UNSATISFIABLE but brute force finds %d solutions"
                             % len(sols_bf))
        return "OK", "unsat"
    if terminal == UNKNOWN:
        if blocks:
            return "WRONG", "UNKNOWN printed with solution blocks"
        return "HONEST", "unknown"
    if terminal is None:
        # only plain satisfy SAT may omit the completion marker
        if all_mode:
            return "WRONG", "-a: no terminal marker at all"
        if sense != 'satisfy':
            return "WRONG", "optimize without completion marker"
        if len(blocks) != 1:
            return "WRONG", "plain satisfy printed %d blocks, want 1" % len(blocks)
    else:  # COMPLETE
        if not blocks:
            return "WRONG", "========== with no solution blocks"
        if not all_mode and sense == 'satisfy':
            return "WRONG", "plain satisfy printed =========="

    seen = set()
    objs = []
    last_val = None
    for blk in blocks:
        val, err = check_block(m, decls, blk)
        if err:
            return "WRONG", err
        proj = projection(val, decls)
        if proj in seen:
            return "WRONG", "duplicate projection printed: %s" % (proj,)
        seen.add(proj)
        last_val = val
        if objvar:
            objs.append(val[objvar])

    if not all_mode and sense != 'satisfy':
        if len(stats_obj) != 1:
            return "WRONG", "objective echo count %d != 1" % len(stats_obj)
        if stats_obj[0] != Fraction(last_val[objvar]):
            return "WRONG", ("echo objective %s != printed solution objective %s"
                             % (stats_obj[0], last_val[objvar]))
        if bf_best is not None and int(stats_obj[0]) != bf_best:
            return "WRONG", ("claimed optimum %s but brute-force optimum is %s"
                             % (stats_obj[0], bf_best))
    if all_mode and sense == 'satisfy':
        want = set(projection(s, decls) for s in sols_bf)
        if seen != want:
            miss = want - seen
            extra = seen - want
            detail = ""
            if miss:
                detail = "missing e.g. %s; " % (next(iter(miss)),)
            if extra:
                detail += "extra e.g. %s" % (next(iter(extra)),)
            return "WRONG", ("enumeration mismatch: bf=%d printed=%d (%s)"
                             % (len(want), len(seen), detail))
    if all_mode and sense != 'satisfy':
        if sense == 'max':
            if not all(objs[i] > objs[i - 1] for i in range(1, len(objs))):
                return "WRONG", "-a maximize objectives not improving: %s" % (objs,)
        else:
            if not all(objs[i] < objs[i - 1] for i in range(1, len(objs))):
                return "WRONG", "-a minimize objectives not improving: %s" % (objs,)
        if bf_best is not None and objs[-1] != bf_best:
            return "WRONG", ("-a final incumbent %s != brute optimum %s"
                             % (objs[-1], bf_best))
    if stats_obj and (all_mode or sense == 'satisfy'):
        return "WRONG", "objective echo present outside plain optimize"
    return "OK", "sat"


def float_check(m, stdout):
    lines = stdout.splitlines()
    blocks, markers, stats_obj = [], [], []
    cur = []
    for ln in lines:
        s = ln.strip()
        if s == BLOCK_END:
            blocks.append(cur); cur = []
        elif s in (COMPLETE, UNSAT, UNKNOWN):
            markers.append(s)
        elif STAT_OBJ.match(s):
            stats_obj.append(Fraction(STAT_OBJ.match(s).group(1)))
        elif s.startswith("%"):
            pass
        elif s:
            cur.append(ln)
    if len(markers) > 1:
        return "WRONG", "multiple terminal markers"
    t = markers[0] if markers else None
    if t == UNKNOWN:
        return ("WRONG", "UNKNOWN with solution blocks") if blocks else ("HONEST", "unknown")
    sense = m.solve[0]
    if t == UNSAT:
        # cross-check exact-ish feasibility via HiGHS on the benign dyadic data
        try:
            from scipy.optimize import linprog
            n = len(m.vars)
            Aub, bub = [], []
            for c in m.cons:
                if c[0].startswith('float_lin_'):
                    row = [Fraction(0)] * n
                    for cf, v in zip(c[1], c[2]):
                        row[names_idx(m, v)] = cf
                    if c[0] == 'float_lin_le':
                        Aub.append(row); bub.append(c[3])
                    elif c[0] == 'float_lin_ge':
                        Aub.append([-v for v in row]); bub.append(-c[3])
                    else:
                        Aub.append(row); bub.append(c[3])
                        Aub.append([-v for v in row]); bub.append(-c[3])
                else:
                    row = [Fraction(0)] * n
                    row[names_idx(m, c[1])] = Fraction(1)
                    Aub.append(row); bub.append(c[3])
                    Aub.append([-v for v in row]); bub.append(-c[2])
            r = linprog([0.0] * n,
                        A_ub=[[float(v) for v in row] for row in Aub] or None,
                        b_ub=[float(v) for v in bub] or None,
                        bounds=[(float(d[1]), float(d[2])) for (_, d) in m.vars],
                        method="highs")
            if r.status == 0:
                return "WRONG", "printed UNSATISFIABLE; feasible model (HiGHS)"
        except ImportError:
            pass
        return "OK", "unsat"
    if len(blocks) != 1:
        return "WRONG", "float: expected exactly one solution block"
    amap, err = parse_block(blocks[0])
    if err:
        return "WRONG", err
    val = {}
    for (nm, dom) in m.vars:
        e = amap.get(nm)
        if e is None or e[0] != "scalar":
            return "WRONG", "float var %s not printed as scalar" % nm
        v = Fraction(e[1])
        bsc = Fraction(1) + max(abs(dom[1]), abs(dom[2]))
        if v < dom[1] - FLOAT_RTOL * bsc or v > dom[2] + FLOAT_RTOL * bsc:
            return "WRONG", "%s=%s outside declared [%s,%s]" % (nm, e[1], dom[1], dom[2])
        val[nm] = v
    max_r = Fraction(0)
    for c in m.cons:
        okc, r = eval_float_constraint(c, val)
        if r > max_r:
            max_r = r
        if not okc:
            return ("WRONG", "float constraint %s violated by printed point "
                    "(scaled residual %s >> 1e-6)" % (c, r))
    if sense != 'satisfy':
        if t != COMPLETE:
            return "WRONG", "optimize SAT without =========="
        if len(stats_obj) != 1:
            return "WRONG", "float objective echo count %d != 1" % len(stats_obj)
        reco = val[m.solve[1]]
        if abs(stats_obj[0] - reco) > Fraction(1, 10**12) * (1 + abs(reco)):
            return "WRONG", "float echo %s != printed objective %s" % (stats_obj[0], reco)
        try:
            from scipy.optimize import linprog
            n = len(m.vars)
            Aub, bub = [], []
            for c in m.cons:
                row = [Fraction(0)] * n
                if c[0].startswith('float_lin_'):
                    for cf, v in zip(c[1], c[2]):
                        row[names_idx(m, v)] = cf
                    if c[0] == 'float_lin_le':
                        Aub.append(row); bub.append(c[3])
                    elif c[0] == 'float_lin_ge':
                        Aub.append([-v for v in row]); bub.append(-c[3])
                    else:
                        Aub.append(row); bub.append(c[3])
                        Aub.append([-v for v in row]); bub.append(-c[3])
                else:
                    row[names_idx(m, c[1])] = Fraction(1)
                    Aub.append(row); bub.append(c[3])
                    Aub.append([-v for v in row]); bub.append(-c[2])
            co = [Fraction(0)] * n
            co[names_idx(m, m.solve[1])] = Fraction(1 if sense == 'min' else -1)
            r = linprog([float(v) for v in co],
                        A_ub=[[float(v) for v in row] for row in Aub] or None,
                        b_ub=[float(v) for v in bub] or None,
                        bounds=[(float(d[1]), float(d[2])) for (_, d) in m.vars],
                        method="highs")
            if r.status == 0:
                ref = Fraction(r.fun).limit_denominator(10**12)
                if sense == 'max':
                    ref = -ref
                if abs(stats_obj[0] - ref) > Fraction(1, 10**6) * (1 + abs(ref)):
                    return "WRONG", ("claimed optimum %s vs reference %s"
                                     % (stats_obj[0], ref))
        except ImportError:
            pass
    return "OK", "sat-float r=%.3g" % float(max_r)


def names_idx(m, nm):
    for i, (vnm, _) in enumerate(m.vars):
        if vnm == nm:
            return i
    raise KeyError(nm)


def run_one(task):
    it, seed = task
    rng = random.Random((seed << 22) + it * 7919)
    if rng.random() < 0.3:
        m = gen_float_model(rng)
        path = os.path.join(TMP, "mf_%d.fzn" % it)
        emit_float_fzn(m, path)
        r = subprocess.run([FZNSOLVE, path], capture_output=True, text=True,
                           timeout=30)
        if r.returncode != 0:
            return ("WRONG", "float exit rc=%d" % r.returncode)
        return float_check(m, r.stdout)
    m = gen_model(rng)
    path = os.path.join(TMP, "m_%d.fzn" % it)
    decls = emit_fzn(m, path, True, rng)
    all_mode = rng.random() < 0.35
    cmd = [FZNSOLVE] + (["-a"] if all_mode else []) + [path]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return ("WRONG", "timeout on well-formed small model")
    if r.returncode != 0:
        return ("WRONG", "exit rc=%d stderr=%s" % (r.returncode, r.stderr.strip()[:100]))
    return check_model(m, decls, r.stdout, all_mode)


def _set_pin_text(member_csv, eq_val):
    return ("var {%s}: x :: output_var;\n"
            "constraint int_lin_eq([1],[x], %d);\nsolve satisfy;\n" % (member_csv, eq_val))


PINS = [
    ("abs_unsat_1", "var 0..1: x0 :: output_var;\nvar -4..-3: x1 :: output_var;\n"
                    "constraint int_abs(x1, x0);\nsolve satisfy;\n", UNSAT, None),
    ("abs_unsat_fixed", "var 5..5: x0 :: output_var;\nvar -4..-3: x1 :: output_var;\n"
                        "constraint int_abs(x1, x0);\nsolve satisfy;\n", UNSAT, None),
    ("abs_feasible", "var 3..4: x0 :: output_var;\nvar -4..-3: x1 :: output_var;\n"
                     "constraint int_abs(x1, x0);\nsolve satisfy;\n", "SAT",
     ("x0 = 3;", "x1 = -3;")),
    # 65536-member set domain: CP materializes it (boundary of CP_MAXVALS).
    ("set65536_eq", _set_pin_text(",".join(str(i) for i in range(1, 65537)), 40000),
     "SAT", ("x = 40000;",)),
    # 66000-member set domain: CP declines (> CP_MAXVALS) to the MIP/SOS1
    # bridge.  Pre-change the parser's fixed 256-entry stack buffer
    # over-read and the CP init wrote an empty domain -> SIGSEGV.
    ("bigset_eq", _set_pin_text(",".join(str(i) for i in range(1, 66001)), 54321),
     "SAT", ("x = 54321;",)),
]


def run_pins():
    bad = []
    for name, text, want, needle in PINS:
        p = os.path.join(TMP, "pin_%s.fzn" % name)
        open(p, "w").write(text)
        r = subprocess.run([FZNSOLVE, p], capture_output=True, text=True, timeout=60)
        if want == UNSAT:
            if r.stdout.strip() != UNSAT:
                bad.append("%s: want %s, got %r" % (name, UNSAT, r.stdout.strip()[:60]))
        else:
            if any(n not in r.stdout for n in needle):
                bad.append("%s: want %s, got %r" % (name, needle, r.stdout.strip()[:60]))
    return bad


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260815
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    os.makedirs(TMP, exist_ok=True)
    bad = run_pins()
    if bad:
        for b in bad:
            print("WRONG pin:", b)
        print("fzn_output_check: pin failures: %d" % len(bad))
        # Pins failing is already a checker-detected fabrication -> exit 1.
        # PSOLVE_FZNOUT_SKIP_PINS=1 exists ONLY to calibrate the checker
        # itself (run against a known-bad binary to count corpus catches);
        # it is never set in test.sh or any gate.
        if os.environ.get("PSOLVE_FZNOUT_SKIP_PINS") != "1":
            return 1
        print("fzn_output_check: pins bypassed for CALIBRATION ONLY")
        bad = []
    ok = wrong = honest = 0
    maxr = 0.0
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for it, res in zip(range(N), ex.map(run_one, [(i, seed) for i in range(N)])):
            v, d = res
            if v == "OK":
                ok += 1
                if d.startswith("sat-float r="):
                    maxr = max(maxr, float(d.rsplit("=", 1)[1]))
            elif v == "HONEST":
                honest += 1
            else:
                wrong += 1
                bad.append((it, d))
    print("fzn_output_check: models=%d OK=%d HONEST=%d WRONG=%d (seed=%d)"
          " float_max_scaled_resid=%.3g" % (N, ok, honest, wrong, seed, maxr))
    for it, d in bad[:15]:
        print("WRONG model %d: %s" % (it, d))
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
