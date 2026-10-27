#!/usr/bin/env python3
"""Differential verifier for psolve's native reified/half-reified
value_precede_chain_int and seq_precede_chain_int FlatZinc predicates.

Enumerates ALL solutions with `-a` (every matrix cell + bool declared as
output) and compares the exact feasible (assignment, b) SET against a
brute-force truth table.  Covers both full reification (_reif: b <-> cond)
and implication (_imp: b -> cond), plus the non-reified forms.

value_precede_chain(order, xs): for consecutive values s=order[i], t=order[i+1],
  if t appears at position k, then s must appear at some earlier position j<k.
seq_precede_chain(xs): value_precede_chain over the whole contiguous domain
  lo..hi of xs (all domain values in increasing order).

No external deps.
"""
import itertools
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER = os.path.join(ROOT, "fznsolve")
SCRATCH = os.path.join(ROOT, "scratch")
os.makedirs(SCRATCH, exist_ok=True)


def vpc_holds(xs, order):
    """if order[i+1] at pos k => order[i] at some pos j<k."""
    n = len(xs)
    for i in range(len(order) - 1):
        s, t = order[i], order[i + 1]
        for k in range(n):
            if xs[k] == t:
                if not any(xs[j] == s for j in range(k)):
                    return False
    return True


def seq_holds(xs, lo, hi):
    return vpc_holds(xs, list(range(lo, hi + 1)))


def gen(kind, form, n, domain, order, allsol):
    """kind: 'vpc'|'seq'; form: ''|'_reif'|'_imp'."""
    lo, hi = domain
    base = "fzn_value_precede_chain_int" if kind == "vpc" else "fzn_seq_precede_chain_int"
    pred = base + form
    lines = []
    if kind == "vpc":
        if form:
            lines.append("predicate %s(array[int] of int: c, array[int] of var int: x, var bool: b);" % pred)
        else:
            lines.append("predicate %s(array[int] of int: c, array[int] of var int: x);" % pred)
        cvals = ",".join(str(v) for v in order)
        for i in range(1, n + 1):
            lines.append("var %d..%d: x%d :: output_var;" % (lo, hi, i))
        xlist = ",".join("x%d" % i for i in range(1, n + 1))
        if form:
            lines.append("var bool: b :: output_var;")
            lines.append("constraint %s([%s],[%s],b);" % (pred, cvals, xlist))
        else:
            lines.append("constraint %s([%s],[%s]);" % (pred, cvals, xlist))
    else:
        if form:
            lines.append("predicate %s(array[int] of var int: x, var bool: b);" % pred)
        else:
            lines.append("predicate %s(array[int] of var int: x);" % pred)
        for i in range(1, n + 1):
            lines.append("var %d..%d: x%d :: output_var;" % (lo, hi, i))
        xlist = ",".join("x%d" % i for i in range(1, n + 1))
        if form:
            lines.append("var bool: b :: output_var;")
            lines.append("constraint %s([%s],b);" % (pred, xlist))
        else:
            lines.append("constraint %s([%s]);" % (pred, xlist))
    lines.append("solve satisfy;")
    return "\n".join(lines) + "\n"


def run(src, allsol):
    fd, path = tempfile.mkstemp(suffix=".fzn", dir=SCRATCH)
    try:
        with os.fdopen(fd, "w") as f:
            f.write(src)
        cmd = [SOLVER] + (["-a"] if allsol else []) + [path]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return r.stdout
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def parse_solutions(out, n, has_b):
    sols = set()
    status = "sat"
    cur = {}

    def flush():
        return dict(cur)
    blocks = []
    for ln in out.splitlines():
        s = ln.strip()
        if s in ("----------", "=========="):
            blocks.append(flush()); cur = {}
            continue
        if s == "=====UNSATISFIABLE=====":
            status = "unsat"; continue
        if s == "=====UNKNOWN=====":
            status = "unknown"; continue
        m = re.match(r"^(\w+)\s*=\s*(.*);$", s)
        if m:
            cur[m.group(1)] = m.group(2).strip()
    if cur:
        blocks.append(flush())
    for blk in blocks:
        xs = []
        good = True
        for i in range(1, n + 1):
            key = "x%d" % i
            if key not in blk:
                good = False; break
            try:
                xs.append(int(blk[key]))
            except ValueError:
                good = False; break
        if not good:
            continue
        b = None
        if has_b:
            bt = blk.get("b", "").lower()
            b = 1 if bt in ("true", "1") else (0 if bt in ("false", "0") else None)
            if b is None:
                continue
        sols.add((tuple(xs), b))
    return sols, status


def brute_set(kind, form, n, domain, order):
    lo, hi = domain
    out = set()
    for xs in itertools.product(range(lo, hi + 1), repeat=n):
        if kind == "vpc":
            t = vpc_holds(list(xs), order)
        else:
            t = seq_holds(list(xs), lo, hi)
        if form == "_reif":
            out.add((tuple(xs), 1 if t else 0))
        elif form == "_imp":
            # b -> cond: b=1 requires t; b=0 always allowed
            if t:
                out.add((tuple(xs), 0)); out.add((tuple(xs), 1))
            else:
                out.add((tuple(xs), 0))
        else:
            if t:
                out.add((tuple(xs), None))
    return out


def main():
    ok = wrong = unknown = 0
    fails = []
    cases = []
    for kind in ("vpc", "seq"):
        for form in ("", "_reif", "_imp"):
            # small domains so brute force is cheap; n=1 catches the
            # non-vacuous single-element precedence case (audit finding)
            for n, domain in ((1, (1, 2)), (2, (1, 2)), (3, (1, 2)),
                              (4, (1, 2)), (3, (1, 3)), (4, (0, 2))):
                if kind == "vpc":
                    # order is a subset/permutation of the domain
                    orders = [[1, 2], [2, 1]]
                    if domain[1] >= 3:
                        orders += [[1, 2, 3], [3, 1, 2], [1, 3]]
                    for order in orders:
                        cases.append((kind, form, n, domain, order))
                else:
                    cases.append((kind, form, n, domain, None))

    for (kind, form, n, domain, order) in cases:
        src = gen(kind, form, n, domain, order, True)
        out = run(src, True)
        has_b = (form != "")
        got, status = parse_solutions(out, n, has_b)
        want = brute_set(kind, form, n, domain, order)
        tag = "%s%s n=%d dom=%s order=%s" % (kind, form, n, domain, order)
        if status == "unknown":
            unknown += 1
            continue
        if status == "unsat":
            if want:
                wrong += 1; fails.append(("unsat-but-sat", tag))
            else:
                ok += 1
            continue
        if got != want:
            extra = got - want
            missing = want - got
            wrong += 1
            fails.append(("set-mismatch", tag, "extra=%d missing=%d" % (len(extra), len(missing)),
                          "extra_e=%s" % list(extra)[:2], "miss_e=%s" % list(missing)[:2]))
        else:
            ok += 1

    print("vpc_reif_verify: exact_set=%d WRONG=%d UNKNOWN=%d (total=%d)"
          % (ok, wrong, unknown, ok + wrong + unknown))
    for f in fails[:25]:
        print("  FAIL", f)
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
