#!/usr/bin/env python3
"""CP-SAT reference oracle for FlatZinc files, built on OR-Tools' official API.

Why this exists: the MiniZinc bundle's `fzn-cp-sat` binary cannot be installed
in this sandbox (GitHub release assets redirect to a host outside the
allow-list, and the PyPI wheels carry no FlatZinc front end).  To still get an
independent CP-SAT baseline, this tool parses a `.fzn` file and states the
same model through `ortools.sat.python.cp_model`, then solves it with one
search worker -- the same configuration `fzn-cp-sat -p 1` uses.

Usage (a subset of the fzn-cp-sat CLI):

    fzn_cpsat_ref.py [options] model.fzn
        -a, --all-solutions   enumerate every solution (like -a)
        -n <k>                cap on enumerated solutions (default 10000)
        -p <k>                number of CP-SAT workers (default 1)
        -t <s>                time limit in seconds
        --time                report parse/build/solve timings on stderr
        --json <path>         dump {status, parse_s, build_s, solve_s, total_s,
                              solutions, unsupported}

Output mimics FlatZinc conventions: a solution block per solution terminated
by `----------`, then `==========` (satisfied/optimal), `=====UNSATISFIABLE=====`
or `=====UNKNOWN=====`.

Anything it cannot translate is reported (`UNSUPPORTED ...` on stderr and in
the JSON) instead of being silently dropped.
"""
import argparse
import json
import re
import sys
import time

from ortools.sat.python import cp_model

# ---------------------------------------------------------------------------
# FlatZinc parsing
# ---------------------------------------------------------------------------


class ParseError(Exception):
    pass


class Unsupported(Exception):
    pass


def strip_comments(text):
    return "\n".join(line.split("%", 1)[0] for line in text.splitlines())


def statements(text):
    out, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == ";" and depth == 0:
            out.append(text[start:i].strip())
            start = i + 1
    tail = text[start:].strip()
    if tail:
        out.append(tail)
    return [s for s in out if s]


def split_args(s):
    out, depth, start = [], 0, 0
    for i, ch in enumerate(s):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            out.append(s[start:i].strip())
            start = i + 1
    out.append(s[start:].strip())
    return out if any(out) else []


def parse_call(expr):
    m = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*)\s*\((.*)\)", expr.strip(), re.S)
    if not m:
        raise ParseError(f"not a call: {expr!r}")
    return m.group(1), split_args(m.group(2))


def parse_value(expr, scalars):
    """Evaluate a ground FlatZinc expression into python data."""
    expr = expr.strip()
    if expr in ("true", "false"):
        return expr == "true"
    if re.fullmatch(r"-?\d+", expr):
        return int(expr)
    if expr.startswith("[") and expr.endswith("]"):
        inner = expr[1:-1].strip()
        return [parse_value(e, scalars) for e in split_args(inner)] if inner else []
    if expr.startswith("{") and expr.endswith("}"):
        inner = expr[1:-1].strip()
        return sorted({parse_value(e, scalars) for e in split_args(inner)}) if inner else []
    if expr in scalars:
        return scalars[expr]
    raise ParseError(f"cannot evaluate {expr!r}")


DECL_VAR = re.compile(r"var\s+(?P<dom>.+?)\s*:\s*(?P<name>[A-Za-z_][A-Za-z_0-9]*)$")
DECL_ARR = re.compile(
    r"array\s*\[\s*(?P<lo>-?\d+)\s*\.\.\s*(?P<hi>-?\d+)\s*\]\s*of\s+"
    r"(?P<kind>var|int|bool)\s*(?P<dom>[^{:=]+?)?\s*:\s*"
    r"(?P<name>[A-Za-z_][A-Za-z_0-9]*)(?:\s*=\s*(?P<val>.+))?$", re.S)
DECL_SCALAR = re.compile(r"(?:set\s+of\s+)?(?P<kind>int|bool)\s*:\s*"
                         r"(?P<name>[A-Za-z_][A-Za-z_0-9]*)(?:\s*=\s*(?P<val>.+))?$", re.S)


class Model:
    """Ground FlatZinc model: declarations, constraints, solve item."""

    def __init__(self):
        self.int_vars = {}        # name -> (lo, hi, values|None)
        self.bool_vars = set()
        self.scalars = {}
        self.arrays = {}          # name -> (offset, values|None)
        self.var_arrays = {}      # name -> (offset, size, 'int'|'bool')
        self.alias_arrays = {}    # name -> (offset, [var names])
        self.constraints = []     # (name, [raw args], [annotations])
        self.solve = None
        self.outputs = []
        self.output_arrays = []
        self.declared_order = []

    # -- declaration bookkeeping ------------------------------------------
    def declare_domain(self, name, dom):
        self.int_vars[name] = dom
        self.declared_order.append(name)

    def declare_bool(self, name):
        self.bool_vars.add(name)
        self.declared_order.append(name)


def parse_domain(text):
    text = text.strip()
    if text == "bool":
        return (0, 1, None)
    if text in ("int", "var int"):
        raise Unsupported("unbounded int")
    if re.search(r"\d\.\d", text):
        raise Unsupported("float domain")
    m = re.fullmatch(r"(-?\d+)\s*\.\.\s*(-?\d+)", text)
    if m:
        return (int(m.group(1)), int(m.group(2)), None)
    if text.startswith("{") and text.endswith("}"):
        vals = parse_value(text, {})
        return (vals[0], vals[-1], tuple(vals))
    raise ParseError(f"bad domain {text!r}")


def parse_fzn(text):
    text = strip_comments(text)
    mdl = Model()
    pending = []
    for stmt in statements(text):
        parts = [p.strip() for p in re.split(r"\s+::\s+", stmt)]
        head, anns = parts[0], parts[1:]
        for ann in list(anns):
            # `array [1..n] of var int: x :: output_array([1..n]) = [a,b,...]`
            if re.search(r"\s=\s*\[", ann):
                pre, val = re.split(r"\s=\s*", ann, maxsplit=1)
                anns[anns.index(ann)] = pre.strip()
                head = head + " = " + val.strip()
                break
        if head.startswith("predicate "):
            continue
        if head.startswith("solve"):
            mdl.solve = (head, anns)
            continue
        if head.startswith("constraint "):
            pending.append((head[len("constraint "):].strip(), anns))
            continue
        _declare(mdl, head, anns)
    for expr, anns in pending:
        name, args = parse_call(expr)
        mdl.constraints.append((name, args, anns))
    return mdl


def _declare(mdl, head, anns):
    m = DECL_ARR.match(head)
    if m:
        lo, hi = int(m.group("lo")), int(m.group("hi"))
        kind, name, val = m.group("kind"), m.group("name"), m.group("val")
        dom = (m.group("dom") or "").strip()
        size = max(0, hi - lo + 1)
        if val is None and kind == "var":
            if dom == "bool":
                mdl.var_arrays[name] = (lo, size, "bool")
                for i in range(size):
                    mdl.declare_bool(f"{name}[{lo + i}]")
            else:
                elem = parse_domain(dom)
                mdl.var_arrays[name] = (lo, size, "int")
                for i in range(size):
                    mdl.declare_domain(f"{name}[{lo + i}]", elem)
            if any("output_array" in a for a in anns):
                mdl.output_arrays.append((name, lo, size))
            return
        if kind == "var":
            names = [e.strip() for e in split_args(val.strip()[1:-1])]
            if not all(re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", e) for e in names):
                raise Unsupported(f"var array literal for {name}")
            mdl.alias_arrays[name] = (lo, names)
            if any("output_array" in a for a in anns):
                mdl.output_arrays.append((name, lo, size))
            return
        data = parse_value(val, mdl.scalars)
        mdl.arrays[name] = (lo, data)
        if any("output_array" in a for a in anns):
            mdl.output_arrays.append((name, lo, size))
        return
    m = DECL_VAR.match(head)
    if m:
        dom, name = m.group("dom").strip(), m.group("name")
        if dom == "bool":
            mdl.declare_bool(name)
        else:
            mdl.declare_domain(name, parse_domain(dom))
        if any("output_var" in a for a in anns):
            mdl.outputs.append(name)
        return
    m = DECL_SCALAR.match(head)
    if m:
        name, val = m.group("name"), m.group("val")
        if val is None:
            mdl.scalars[name] = None
        else:
            mdl.scalars[name] = parse_value(val, mdl.scalars)
        return
    raise ParseError(f"unsupported declaration {head!r}")


# ---------------------------------------------------------------------------
# CP-SAT construction
# ---------------------------------------------------------------------------


class Builder:
    def __init__(self):
        self.m = cp_model.CpModel()
        self.ivars = {}
        self.bvars = {}
        self.arrays = {}          # name -> (offset, [int|IntVar])
        self.var_arrays = {}
        self.bounds = {}          # id(IntVar) -> (lo, hi, values)
        self.unsupported = []

    # -- variables ---------------------------------------------------------
    def int_var(self, name):
        if name in self.ivars:
            return self.ivars[name]
        dom = self.model_int_domains.get(name)
        if dom is None:
            raise Unsupported(f"unknown var {name}")
        lo, hi, vals = dom
        if vals is None:
            v = self.m.new_int_var(lo, hi, name)
        else:
            v = self.m.new_int_var_from_domain(
                cp_model.Domain.from_values(list(vals)), name)
        self.ivars[name] = v
        self.bounds[id(v)] = (lo, hi, vals)
        return v

    def bool_var(self, name):
        if name in self.bvars:
            return self.bvars[name]
        b = self.m.new_bool_var(name)
        self.bvars[name] = b
        return b

    def var(self, name):
        if name in self.bool_vars:
            return self.bool_var(name)
        return self.int_var(name)

    def is_var_name(self, name):
        return (name in self.ivars or name in self.bvars
                or name in getattr(self, "bool_vars", ())
                or name in getattr(self, "model_int_domains", {}))

    def known(self, name):
        return (name in self.ivars or name in self.bvars or name in self.bool_vars
                or name in self.model_int_domains or name in self.arrays
                or name in self.var_arrays or name in self.scalars)

    # -- argument resolution ----------------------------------------------
    def res(self, expr):
        expr = expr.strip()
        if not expr:
            raise Unsupported("empty argument")
        if expr in ("true", "false"):
            return 1 if expr == "true" else 0
        if re.fullmatch(r"-?\d+", expr):
            return int(expr)
        if expr.startswith("array2d(") and expr.endswith(")"):
            parts = split_args(expr[len("array2d("):-1])
            lo, hi, w, h = (int(self.res(p)) for p in parts[:4])
            return self.res(parts[4])
        if expr.startswith("[") and expr.endswith("]"):
            inner = expr[1:-1].strip()
            return [self.res(e) for e in split_args(inner)] if inner else []
        if expr.startswith("{") and expr.endswith("}"):
            return list(self.res(e) for e in split_args(expr[1:-1]))
        m = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*)\s*([+-])\s*(\d+)", expr)
        if m and self.is_var_name(m.group(1)):
            base = self.var(m.group(1))
            k = int(m.group(3)) * (1 if m.group(2) == "+" else -1)
            lo, hi, vals = self.bounds[id(base)]
            aux = self.m.new_int_var(lo + k, hi + k, "")
            self.m.add(aux == base + k)
            self.bounds[id(aux)] = (lo + k, hi + k,
                                    None if vals is None else tuple(v + k for v in vals))
            return aux
        m = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*)\s*\[\s*(-?\d+)\s*\]", expr)
        if m:
            name, idx = m.group(1), int(m.group(2))
            if name in self.arrays or name in self.var_arrays or name in self.alias_arrays:
                arr = self.res(name)
                off = self.array_offset(name)
                if not (0 <= idx - off < len(arr)):
                    raise Unsupported(f"index {idx} out of range for {name}")
                return arr[idx - off]
            return self.var(f"{name}[{idx}]")
        m = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*)", expr)
        if m:
            name = m.group(1)
            if name in self.scalars:
                return self.scalars[name]
            if name in self.arrays:
                off, data = self.arrays[name]
                return list(data)
            if name in self.alias_arrays:
                off, names = self.alias_arrays[name]
                return [self.var(n) for n in names]
            if name in self.var_arrays:
                off, size, kind = self.var_arrays[name]
                return [self.var(f"{name}[{off + i}]") for i in range(size)]
            if self.known(name):
                return self.var(name)
            raise Unsupported(f"unknown identifier {name}")
        raise Unsupported(f"unsupported argument {expr!r}")

    def array_offset(self, name):
        for table in (self.arrays, self.var_arrays, self.alias_arrays):
            if name in table:
                return table[name][0]
        return 1

    def plain(self, arr):
        """Coerce a resolved array into a list of IntVar/int entries."""
        return arr if isinstance(arr, list) else [arr]

    def lit(self, x):
        return x if not isinstance(x, int) else (1 if x else 0)

    def enforce(self, x, cond):
        self.m.add(x == cond)

    # -- posting -----------------------------------------------------------
    def build(self, mdl):
        self.model_int_domains = mdl.int_vars
        self.bool_vars = mdl.bool_vars
        self.scalars = mdl.scalars
        self.arrays = {k: (off, list(data)) for k, (off, data) in mdl.arrays.items()}
        self.var_arrays = dict(mdl.var_arrays)
        self.alias_arrays = dict(mdl.alias_arrays)
        for name, dom in mdl.int_vars.items():
            if dom is not None and re.fullmatch(r".*\[-?\d+\]", name):
                continue
            if dom is None:
                continue
            self.int_var(name)
        for name in mdl.bool_vars:
            if "[" not in name:
                self.bool_var(name)
        for name, args, anns in mdl.constraints:
            try:
                self.post(name, [a for a in args])
            except Unsupported as exc:
                self.unsupported.append(f"{name}: {exc}")
        self.apply_solve(mdl)
        return self.unsupported

    def post(self, name, args):
        for cand in (name, _canon(name)):
            handler = getattr(self, "c_" + cand, None)
            if handler is not None:
                return handler(args)
        raise Unsupported(name)

    # -- base constraints --------------------------------------------------
    def rel(self, args, op):
        x, y = self.res(args[0]), self.res(args[1])
        self.m.add(op(x, y))

    def c_int_eq(self, a):
        self.rel(a, lambda x, y: x == y)

    def c_int_ne(self, a):
        self.rel(a, lambda x, y: x != y)

    def c_int_le(self, a):
        self.rel(a, lambda x, y: x <= y)

    def c_int_lt(self, a):
        self.rel(a, lambda x, y: x < y)

    def c_int_ge(self, a):
        self.rel(a, lambda x, y: x >= y)

    def c_int_gt(self, a):
        self.rel(a, lambda x, y: x > y)

    c_bool_eq = c_int_eq
    c_bool_ne = c_int_ne
    c_bool_le = c_int_le
    c_bool_lt = c_int_lt
    c_bool_ge = c_int_ge
    c_bool_gt = c_int_gt

    def _lin(self, a):
        coeffs = self.res(a[0])
        vars_ = self.plain(self.res(a[1]))
        rhs = self.res(a[2])
        expr = 0
        for c, v in zip(coeffs, vars_):
            if isinstance(v, int):
                rhs -= c * v
            else:
                expr = expr + c * v
        return expr, rhs

    def c_int_lin_eq(self, a):
        e, r = self._lin(a)
        self.m.add(e == r)

    def c_int_lin_ne(self, a):
        e, r = self._lin(a)
        self.m.add(e != r)

    def c_int_lin_le(self, a):
        e, r = self._lin(a)
        self.m.add(e <= r)

    def c_int_lin_lt(self, a):
        e, r = self._lin(a)
        self.m.add(e < r)

    def c_int_lin_ge(self, a):
        e, r = self._lin(a)
        self.m.add(e >= r)

    def c_int_lin_gt(self, a):
        e, r = self._lin(a)
        self.m.add(e > r)

    c_bool_lin_eq = c_int_lin_eq
    c_bool_lin_ne = c_int_lin_ne
    c_bool_lin_le = c_int_lin_le
    c_bool_lin_lt = c_int_lin_lt
    c_bool_lin_ge = c_int_lin_ge
    c_bool_lin_gt = c_int_lin_gt

    def c_bool_not(self, a):
        self.m.add(self.lit(self.res(a[0])) == 0)

    def c_bool_and(self, a):
        lits = [self.lit(v) for v in self.plain(self.res(a[0]))]
        self.m.add_bool_and(lits).only_enforce_if(self.lit(self.res(a[1])))

    def c_bool_or(self, a):
        lits = [self.lit(v) for v in self.plain(self.res(a[0]))]
        self.m.add_bool_or(lits).only_enforce_if(self.lit(self.res(a[1])))

    def c_bool_xor(self, a):
        lits = [self.lit(v) for v in self.plain(self.res(a[0]))]
        self.m.add_bool_xor(lits)

    def c_bool_clause(self, a):
        pos_raw = self.plain(self.res(a[0]))
        neg_raw = self.plain(self.res(a[1]))
        if any(isinstance(v, int) and v == 1 for v in pos_raw):
            return
        if any(isinstance(v, int) and v == 0 for v in neg_raw):
            return
        lits = [v for v in pos_raw if not isinstance(v, int)]
        lits += [v.negated() for v in neg_raw if not isinstance(v, int)]
        if lits:
            self.m.add_bool_or(lits)

    def c_bool2int(self, a):
        self.m.add(self.lit(self.res(a[1])) == self.res(a[0]))

    # -- globals -----------------------------------------------------------
    def c_all_different_int(self, a):
        self.m.add_all_different(self.plain(self.res(a[0])))

    def c_all_different_except_0(self, a):
        xs = self.plain(self.res(a[0]))
        for i in range(len(xs)):
            for j in range(i + 1, len(xs)):
                b1, b2 = self.m.new_bool_var(""), self.m.new_bool_var("")
                self.m.add(xs[i] != 0).only_enforce_if(b1)
                self.m.add(xs[i] == 0).only_enforce_if(b1.negated())
                self.m.add(xs[j] != 0).only_enforce_if(b2)
                self.m.add(xs[j] == 0).only_enforce_if(b2.negated())
                self.m.add(xs[i] != xs[j]).only_enforce_if(self.and_lit([b1, b2]))

    def c_all_different_except(self, a):
        self.c_all_different_except_0(a)

    def c_all_equal_int(self, a):
        xs = self.plain(self.res(a[0]))
        for x in xs[1:]:
            self.m.add(x == xs[0])

    c_all_equal_bool = c_all_equal_int

    def c_increasing_int(self, a):
        xs = self.plain(self.res(a[0]))
        for x, y in zip(xs, xs[1:]):
            self.m.add(x <= y)

    def c_strictly_increasing_int(self, a):
        xs = self.plain(self.res(a[0]))
        for x, y in zip(xs, xs[1:]):
            self.m.add(x < y)

    def c_decreasing_int(self, a):
        xs = self.plain(self.res(a[0]))
        for x, y in zip(xs, xs[1:]):
            self.m.add(x >= y)

    def c_strictly_decreasing_int(self, a):
        xs = self.plain(self.res(a[0]))
        for x, y in zip(xs, xs[1:]):
            self.m.add(x > y)

    c_increasing_bool = c_increasing_int
    c_decreasing_bool = c_decreasing_int

    def _count(self, xs, val):
        bools = []
        for x in xs:
            b = self.m.new_bool_var("")
            self.m.add(x == val).only_enforce_if(b)
            self.m.add(x != val).only_enforce_if(b.negated())
            bools.append(b)
        return bools

    def c_count(self, a):
        xs = self.plain(self.res(a[0]))
        val = self.res(a[1])
        n = self.res(a[2])
        self.m.add(sum(self._count(xs, val)) == n)

    def c_count_eq(self, a):
        xs = self.plain(self.res(a[0]))
        val = self.res(a[1])
        n = self.res(a[2])
        self.m.add(sum(self._count(xs, val)) == n)

    def _count_cmp(self, a, op):
        xs = self.plain(self.res(a[0]))
        val = self.res(a[1])
        n = self.res(a[2])
        self.m.add(op(sum(self._count(xs, val)), n))

    def c_count_geq(self, a):
        self._count_cmp(a, lambda e, n: e >= n)

    def c_count_leq(self, a):
        self._count_cmp(a, lambda e, n: e <= n)

    def c_count_gt(self, a):
        self._count_cmp(a, lambda e, n: e > n)

    def c_count_lt(self, a):
        self._count_cmp(a, lambda e, n: e < n)

    def c_count_neq(self, a):
        xs = self.plain(self.res(a[0]))
        val = self.res(a[1])
        n = self.res(a[2])
        self.m.add(sum(self._count(xs, val)) != n)

    def c_nvalue(self, a):
        n = self.res(a[0])
        xs = self.plain(self.res(a[1]))
        values = sorted({v for x in xs for v in self.domain_values(x)})
        used = []
        for v in values:
            b = self.m.new_bool_var(f"nv{v}")
            present = self._count([x for x in xs if v in self.domain_values(x)], v)
            for p in present:
                self.m.add_implication(p, b)
            if present:
                self.m.add_bool_or(present).only_enforce_if(b)
            used.append(b)
        self.m.add(sum(used) == n)

    def c_global_cardinality(self, a):
        xs = self.plain(self.res(a[0]))
        cover = self.plain(self.res(a[1]))
        counts = self.plain(self.res(a[2]))
        for v, c in zip(cover, counts):
            self.m.add(sum(self._count([x for x in xs if v in self.domain_values(x)], v)) == c)

    def c_global_cardinality_closed(self, a):
        self.c_global_cardinality(a)
        xs = self.plain(self.res(a[0]))
        cover = sorted({int(v) for v in self.plain(self.res(a[1]))})
        for x in xs:
            if not isinstance(x, int):
                self.m.add_allowed_assignments([x], [(v,) for v in cover])

    def c_global_cardinality_low_up(self, a):
        xs = self.plain(self.res(a[0]))
        cover = self.plain(self.res(a[1]))
        los = self.plain(self.res(a[2]))
        his = self.plain(self.res(a[3]))
        for v, lo, hi in zip(cover, los, his):
            n = self.m.new_int_var(lo, hi, f"gcc{v}")
            self.m.add(sum(self._count(xs, v)) == n)

    def c_global_cardinality_low_up_closed(self, a):
        self.c_global_cardinality_low_up(a)
        xs = self.plain(self.res(a[0]))
        cover = sorted({int(v) for v in self.plain(self.res(a[1]))})
        for x in xs:
            if not isinstance(x, int):
                self.m.add_allowed_assignments([x], [(v,) for v in cover])

    def c_table_int(self, a):
        xs = self.plain(self.res(a[0]))
        raw = self.res(a[1])
        if raw and all(isinstance(v, int) for v in raw):
            w = len(xs)
            tuples = [raw[i:i + w] for i in range(0, len(raw) - w + 1, w)]
        else:
            tuples = [[int(v) for v in t] for t in raw]
        self.m.add_allowed_assignments(xs, [t for t in tuples if len(t) == len(xs)])

    c_table = c_table_int

    def c_array_int_element(self, a):
        idx = self.res(a[0])
        arr = self.plain(self.res(a[1]))
        off = self.array_offset(a[1].strip())
        tgt = self.res(a[2])
        ivars = [x if not isinstance(x, int) else self.m.new_constant(x) for x in arr]
        if isinstance(idx, int):
            self.m.add(ivars[idx - off] == tgt)
            return
        index = idx if off == 0 else idx - off
        self.m.add_element(index, ivars, tgt)

    def c_array_var_int_element(self, a):
        self.c_array_int_element(a)

    def c_array_int_maximum(self, a):
        out = self.res(a[0])
        self.m.add_max_equality(out, self.plain(self.res(a[1])))

    def c_array_int_minimum(self, a):
        out = self.res(a[0])
        self.m.add_min_equality(out, self.plain(self.res(a[1])))

    def c_member_int(self, a):
        x = self.res(a[0])
        xs = self.plain(self.res(a[1]))
        self.m.add_allowed_assignments([x], [(v,) for v in self.values_of(xs)])

    def c_int_abs(self, a):
        self.m.add_abs_equality(self.res(a[1]), self.res(a[0]))

    def c_int_times(self, a):
        self.m.add_multiplication_equality(self.res(a[2]), [self.res(a[0]), self.res(a[1])])

    def c_int_div(self, a):
        self.m.add_division_equality(self.res(a[2]), self.res(a[0]), self.res(a[1]))

    def c_int_mod(self, a):
        self.m.add_modulo_equality(self.res(a[2]), self.res(a[0]), self.res(a[1]))

    def c_int_plus(self, a):
        self.m.add(self.res(a[2]) == self.res(a[0]) + self.res(a[1]))

    def c_int_minus(self, a):
        self.m.add(self.res(a[2]) == self.res(a[0]) - self.res(a[1]))

    def c_int_neg(self, a):
        self.m.add(self.res(a[1]) == -self.res(a[0]))

    def c_int_max(self, a):
        self.m.add_max_equality(self.res(a[2]), [self.res(a[0]), self.res(a[1])])

    def c_int_min(self, a):
        self.m.add_min_equality(self.res(a[2]), [self.res(a[0]), self.res(a[1])])

    def c_int_in(self, a):
        x = self.res(a[0])
        vals = self.res(a[1])
        self.m.add_allowed_assignments([x], [(v,) for v in vals])

    c_set_in = c_int_in

    def c_circuit(self, a):
        xs = self.plain(self.res(a[-1]))
        off = self.array_offset(a[-1].strip())
        n = len(xs)
        shifted = []
        for i, x in enumerate(xs):
            y = self.m.new_int_var(0, n - 1, f"circ{i}")
            self.m.add(y == x - off)
            shifted.append(y)
        # one arc per (node, successor) pair, guarded by the successor value
        arcs = []
        for i in range(n):
            for j in range(n):
                if i == j:
                    continue
                lit = self.m.new_bool_var(f"arc{i}_{j}")
                self.m.add(shifted[i] == j).only_enforce_if(lit)
                self.m.add(shifted[i] != j).only_enforce_if(lit.negated())
                arcs.append((i, j, lit))
        self.m.add_circuit(arcs)

    def c_subcircuit(self, a):
        self.c_circuit(a)

    def c_inverse(self, a):
        fs = self.plain(self.res(a[0]))
        gs = self.plain(self.res(a[1]))
        foff = self.array_offset(a[0].strip())
        goff = self.array_offset(a[1].strip())
        f0 = []
        for i, f in enumerate(fs):
            y = self.m.new_int_var(0, len(gs) - 1, f"inv{i}")
            self.m.add(y == f - goff)
            f0.append(y)
        g0 = []
        for j, g in enumerate(gs):
            y = self.m.new_int_var(0, len(fs) - 1, f"invb{j}")
            self.m.add(y == g - foff)
            g0.append(y)
        self.m.add_inverse(f0, g0)

    def c_bin_packing(self, a):
        # FlatZinc bin_packing(capacity, bin, w) / bin_packing_capa(capas, bin, w)
        cap = self.res(a[0])
        binv = self.plain(self.res(a[1]))
        w = self.plain(self.res(a[2]))
        self._bin_packing(binv, w, cap, self.array_offset(a[1].strip()))

    def c_bin_packing_capa(self, a):
        self.c_bin_packing(a)

    def _bin_packing(self, binv, w, cap, off):
        caps = cap if isinstance(cap, list) else [cap] * (max(self.values_of(binv)) + 1)
        for b in range(len(caps)):
            terms = []
            for item, (x, wi) in enumerate(zip(binv, w)):
                lit = self.m.new_bool_var("")
                self.m.add(x == b + off).only_enforce_if(lit)
                self.m.add(x != b + off).only_enforce_if(lit.negated())
                terms.append(lit * wi)
            self.m.add(sum(terms) <= caps[b])

    def c_bin_packing_load(self, a):
        loads = self.plain(self.res(a[0]))
        binv = self.plain(self.res(a[1]))
        w = self.plain(self.res(a[2]))
        off = self.array_offset(a[0].strip())
        for b, lv in enumerate(loads):
            terms = []
            for x, wi in zip(binv, w):
                lit = self.m.new_bool_var("")
                self.m.add(x == b + off).only_enforce_if(lit)
                self.m.add(x != b + off).only_enforce_if(lit.negated())
                terms.append(lit * wi)
            self.m.add(sum(terms) == lv)

    def c_cumulative(self, a):
        s = self.plain(self.res(a[0]))
        d = self.plain(self.res(a[1]))
        r = self.plain(self.res(a[2]))
        cap = self.res(a[3])
        ivs = [self.m.new_interval_var(s[i], d[i], s[i] + d[i], f"itv{i}")
               for i in range(len(s))]
        self.m.add_cumulative(ivs, r, cap)

    def c_disjunctive(self, a):
        s = self.plain(self.res(a[0]))
        d = self.plain(self.res(a[1]))
        ivs = [self.m.new_interval_var(s[i], d[i], s[i] + d[i], f"itv{i}")
               for i in range(len(s))]
        self.m.add_no_overlap(ivs)

    c_disjunctive_strict = c_disjunctive

    def c_diffn(self, a):
        xs = self.plain(self.res(a[0]))
        ys = self.plain(self.res(a[1]))
        dx = self.plain(self.res(a[2]))
        dy = self.plain(self.res(a[3]))
        xiv = [self.m.new_fixed_size_interval_var(xs[i], dx[i], f"dx{i}")
               for i in range(len(xs))]
        yiv = [self.m.new_fixed_size_interval_var(ys[i], dy[i], f"dy{i}")
               for i in range(len(xs))]
        self.m.add_no_overlap_2d(xiv, yiv)

    def _lex(self, a, strict):
        """x <=lex y, and x <lex y when `strict`.

        prefix_i is true iff x[0..i-1] == y[0..i-1]; under prefix_i the
        comparison x[i] <= y[i] must hold.  Strictness additionally needs some
        position where the prefix holds and x[i] < y[i].
        """
        xs = self.plain(self.res(a[0]))
        ys = self.plain(self.res(a[1]))
        n = min(len(xs), len(ys))
        eqs, diffs = [], []
        prefix = None
        for i in range(n):
            eq = self.m.new_bool_var(f"lex_eq{i}")
            self.m.add(xs[i] == ys[i]).only_enforce_if(eq)
            self.m.add(xs[i] != ys[i]).only_enforce_if(eq.negated())
            c = self.m.add(xs[i] <= ys[i])
            lt = self.m.new_bool_var(f"lex_lt{i}")
            self.m.add(xs[i] < ys[i]).only_enforce_if(lt)
            if prefix is not None:
                c.only_enforce_if(prefix)
                both = self.m.new_bool_var(f"lex_pref{i}")
                self.m.add_bool_and([prefix, lt]).only_enforce_if(both)
                self.m.add_bool_or([prefix.negated(), lt.negated()]) \
                    .only_enforce_if(both.negated())
                diffs.append(both)
            else:
                diffs.append(lt)
            eqs.append(eq)
            prefix = eq if prefix is None else self.and_lit([prefix, eq])
        if strict and diffs:
            self.m.add_bool_or(diffs)

    def c_lex_lesseq_int(self, a):
        self._lex(a, strict=False)

    def c_lex_less_int(self, a):
        self._lex(a, strict=True)

    c_lex_lesseq_bool = c_lex_lesseq_int
    c_lex_less_bool = c_lex_less_int

    def and_lit(self, lits):
        b = self.m.new_bool_var("")
        self.m.add_bool_and(list(lits)).only_enforce_if(b)
        self.m.add_bool_or([l.negated() for l in lits]).only_enforce_if(b.negated())
        return b

    # -- solve item --------------------------------------------------------
    def apply_solve(self, mdl, free_search=False):
        if mdl.solve is None:
            return
        head, anns = mdl.solve
        body = head[len("solve"):].strip()
        for kw in ("minimize", "maximize"):
            if body.startswith(kw):
                target = self.res(body[len(kw):].strip())
                if not isinstance(target, int):
                    if kw == "minimize":
                        self.m.minimize(target)
                    else:
                        self.m.maximize(target)
        if not free_search:
            self.apply_search(anns, body)

    def apply_search(self, anns, body):
        for ann in anns:
            if not ann.startswith("int_search"):
                continue
            try:
                name, args = parse_call(ann)
            except ParseError:
                continue
            vars_ = self.plain(self.res(args[0]))
            if any(isinstance(v, int) for v in vars_):
                continue
            sel, val = args[1].strip(), args[2].strip()
            vs = {
                "input_order": cp_model.CHOOSE_FIRST,
                "first_fail": cp_model.CHOOSE_MIN_DOMAIN_SIZE,
                "anti_first_fail": cp_model.CHOOSE_MAX_DOMAIN_SIZE,
                "smallest": cp_model.CHOOSE_LOWEST_MIN,
                "largest": cp_model.CHOOSE_HIGHEST_MAX,
                "most_constrained": cp_model.CHOOSE_MIN_DOMAIN_SIZE,
            }.get(sel, cp_model.CHOOSE_FIRST)
            vv = {
                "indomain_min": cp_model.SELECT_MIN_VALUE,
                "indomain_max": cp_model.SELECT_MAX_VALUE,
                "indomain_median": cp_model.SELECT_LOWER_HALF,
                "indomain_split": cp_model.SELECT_LOWER_HALF,
                "indomain_interval": cp_model.SELECT_LOWER_HALF,
                "indomain_random": cp_model.SELECT_MIN_VALUE,
            }.get(val, cp_model.SELECT_MIN_VALUE)
            self.m.add_decision_strategy(vars_, vs, vv)
            return

    # -- domain helpers ----------------------------------------------------
    def domain_values(self, x):
        if isinstance(x, int):
            return [x]
        b = self.bounds.get(id(x))
        if b is None:
            raise Unsupported("unknown domain")
        lo, hi, vals = b
        return list(vals) if vals is not None else list(range(lo, hi + 1))

    def values_of(self, xs):
        out = set()
        for x in xs:
            out.update(self.domain_values(x))
        return sorted(out)


def _canon(name):
    for prefix in ("fzn_", "ortools_", "gecode_"):
        if name.startswith(prefix):
            return name[len(prefix):]
    return name


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


class Collector(cp_model.CpSolverSolutionCallback):
    def __init__(self, names, limit):
        super().__init__()
        self.names = names
        self.limit = limit
        self.sols = []
        self.stop = False

    def on_solution_callback(self):
        self.sols.append({n: self.value(v) for n, v in self.names})
        if len(self.sols) >= self.limit:
            self.stop = True  # noqa: F841
            self.stop_search()


def run(path, all_solutions=False, limit=10000, workers=1, time_limit=None,
        free_search=False):
    t0 = time.perf_counter()
    with open(path) as fh:
        text = fh.read()
    t1 = time.perf_counter()
    mdl = parse_fzn(text)
    b = Builder()
    b.model_int_domains = mdl.int_vars
    b.bool_vars = mdl.bool_vars
    b.scalars = mdl.scalars
    unsupported = b.build(mdl)
    t2 = time.perf_counter()
    names = []
    for name in mdl.outputs:
        try:
            names.append((name, b.var(name)))
        except Unsupported:
            pass
    for aname, off, size in mdl.output_arrays:
        arr = b.res(aname) if (aname in b.arrays or aname in b.var_arrays) else []
        for i in range(size):
            if i < len(arr) and not isinstance(arr[i], int):
                names.append((f"{aname}[{off + i}]", arr[i]))
    solver = cp_model.CpSolver()
    solver.parameters.num_search_workers = workers
    if time_limit:
        solver.parameters.max_time_in_seconds = time_limit
    sols = []
    if all_solutions:
        solver.parameters.enumerate_all_solutions = True
        cb = Collector(names, limit)
        status = solver.solve(b.m, cb)
        sols = cb.sols
    else:
        status = solver.solve(b.m)
        if status in (cp_model.OPTIMAL, cp_model.FEASIBLE):
            sols = [{n: solver.value(v) for n, v in names}]
    t3 = time.perf_counter()
    # UNSAT is a proof too; only an optimum limited by a time/node cap is not
    proven = status in (cp_model.OPTIMAL, cp_model.INFEASIBLE)
    st = {
        cp_model.OPTIMAL: "SAT",
        cp_model.FEASIBLE: "SAT",
        cp_model.INFEASIBLE: "UNSAT",
        cp_model.UNKNOWN: "UNKNOWN",
        cp_model.MODEL_INVALID: "ERROR",
    }.get(status, "UNKNOWN")
    return {
        "status": st,
        "proven": proven,
        "solutions": sols,
        "parse_s": t1 - t0,
        "build_s": t2 - t1,
        "solve_s": t3 - t2,
        "total_s": t3 - t0,
        "unsupported": unsupported,
        "names": [n for n, _ in names],
    }


def format_output(res):
    lines = []
    for sol in res["solutions"]:
        for name in res["names"]:
            lines.append(f"{name} = {sol[name]};")
        lines.append("----------")
    if res["status"] == "UNSAT":
        lines.append("=====UNSATISFIABLE=====")
    elif res["status"] == "SAT":
        lines.append("==========")
    else:
        lines.append("=====UNKNOWN=====")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="CP-SAT reference oracle for FlatZinc")
    ap.add_argument("model")
    ap.add_argument("-a", "--all-solutions", action="store_true")
    ap.add_argument("-n", type=int, default=10000, dest="limit")
    ap.add_argument("-p", type=int, default=1, dest="workers")
    ap.add_argument("-t", type=float, default=None, dest="time_limit")
    ap.add_argument("-f", "--free-search", action="store_true")
    ap.add_argument("--time", action="store_true")
    ap.add_argument("--json", default=None)
    args = ap.parse_args(argv)
    res = run(args.model, all_solutions=args.all_solutions, limit=args.limit,
              workers=args.workers, time_limit=args.time_limit,
              free_search=args.free_search)
    sys.stdout.write(format_output(res))
    if args.time:
        sys.stderr.write(
            f"parse={res['parse_s']*1000:.2f}ms build={res['build_s']*1000:.2f}ms "
            f"solve={res['solve_s']*1000:.2f}ms total={res['total_s']*1000:.2f}ms\n")
    for u in res["unsupported"]:
        sys.stderr.write(f"UNSUPPORTED {u}\n")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump({k: v for k, v in res.items() if k != "solutions"}, fh)
    return 0 if not res["unsupported"] else 3


if __name__ == "__main__":
    sys.exit(main())
