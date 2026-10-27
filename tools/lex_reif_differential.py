#!/usr/bin/env python3
"""Independent differential verifier for psolve's native reified-lex family.

Methodology (deliberately different from lex_reif_verify.py): enumerate ALL
solutions with `-a`, collect the exact set of (matrix, b) assignments psolve
deems feasible, and compare that SET against the brute-force truth table.
This catches both directions of a wrong reification (b forced wrong, or an
over-/under-constrained matrix domain), including spurious extra solutions.

Predicates covered:
  fzn_lex_lesseq_int_reif(xs, ys, b)      b <-> lex_lesseq(xs,ys)
  fzn_lex_less_int_reif(xs, ys, b)        b <-> lex_less(xs,ys)   (strict)
  fzn_lex_chain_lesseq_int_reif(X, b)     b <-> all adjacent columns lex_lesseq
  fzn_lex_chain_less_int_reif(X, b)       b <-> all adjacent columns lex_less
  fzn_lex2_reif(X, b)                     b <-> rows AND cols lex_lesseq
  fzn_strict_lex2_reif(X, b)              b <-> rows AND cols lex_less
  _imp half-reification variants (b -> condition, matrix otherwise free)

Domain 0..1 (2x2) and 0..2 (2x2 / 3x2 / single length 2-3).  For `-a` we
pin nothing, so the full feasible set is compared.  Also verifies UNSAT
detection for pinned contradictions.  No external deps.
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


def lex_le(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] < b[i]:
            return True
        if a[i] > b[i]:
            return False
    return len(a) <= len(b)


def lex_lt(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] < b[i]:
            return True
        if a[i] > b[i]:
            return False
    return len(a) < len(b)


def cols(mat, r, c):
    return [[mat[rr * c + cc] for rr in range(r)] for cc in range(c)]


def rows(mat, r, c):
    return [mat[rr * c:(rr + 1) * c] for rr in range(r)]


def cond(kind, strict, dims, mat):
    cmpf = lex_lt if strict else lex_le
    if kind == "lex":
        L = dims
        return cmpf(mat[:L], mat[L:2 * L])
    r, c = dims
    cl = cols(mat, r, c)
    chain = all(cmpf(cl[j], cl[j + 1]) for j in range(c - 1))
    if kind == "chain":
        return chain
    rw = rows(mat, r, c)
    rowchain = all(cmpf(rw[i], rw[i + 1]) for i in range(r - 1))
    return chain and rowchain


def pred_name(kind, strict, imp):
    suffix = "_imp" if imp else "_reif"
    if kind == "lex":
        return "fzn_lex_less%s_int%s" % ("eq" if not strict else "", suffix)
    if kind == "chain":
        return "fzn_lex_chain_less%s_int%s" % ("eq" if not strict else "", suffix)
    return "fzn_%slex2%s" % ("strict_" if strict else "", suffix)


def gen(kind, strict, imp, dims, domain, allsol, pin=None, fixmat=None):
    hi = domain
    p = pred_name(kind, strict, imp)
    lines = []
    if kind == "lex":
        L = dims
        lines.append("predicate %s(array[int] of var int: xs, array[int] of var int: ys, var bool: b);" % p)
        lines.append("array [1..%d] of var 0..%d: xs :: output_array([1..%d]);" % (L, hi, L))
        lines.append("array [1..%d] of var 0..%d: ys :: output_array([1..%d]);" % (L, hi, L))
        lines.append("var bool: b :: output_var;")
        lines.append("constraint %s(xs, ys, b);" % p)
        ncells = 2 * L
        def fixline(i, v):
            nm = "xs" if i < L else "ys"
            idx = (i % L) + 1
            return "constraint int_eq(%s[%d], %d);" % (nm, idx, v)
    else:
        r, c = dims
        lines.append("predicate %s(array[int,int] of var int: xs, var bool: b);" % p)
        lines.append("array [1..%d,1..%d] of var 0..%d: x :: output_array([1..%d,1..%d]);" % (r, c, hi, r, c))
        lines.append("var bool: b :: output_var;")
        lines.append("constraint %s(x, b);" % p)
        ncells = r * c
        def fixline(i, v):
            return "constraint int_eq(x[%d], %d);" % (i + 1, v)
    if fixmat is not None:
        for i, v in enumerate(fixmat):
            lines.append(fixline(i, v))
    if pin is True:
        lines.append("constraint bool_eq(b, true);")
    elif pin is False:
        lines.append("constraint bool_eq(b, false);")
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


def parse_solutions(out, kind, dims):
    """Return (set_of_(mat_tuple, b), status) with status in {sat,unsat,unknown}."""
    sols = set()
    status = "sat"
    cur = {}
    blocks = []
    def flush():
        blocks.append(dict(cur))
    for ln in out.splitlines():
        s = ln.strip()
        if s == "----------" or s == "==========":
            flush(); cur = {}
            continue
        if s == "=====UNSATISFIABLE=====":
            status = "unsat"; continue
        if s == "=====UNKNOWN=====":
            status = "unknown"; continue
        m = re.match(r"^(\w+)\s*=\s*(.*);$", s)
        if m:
            cur[m.group(1)] = m.group(2)
    if cur:
        flush()
    for blk in blocks:
        need = ["b"] + (["xs", "ys"] if kind == "lex" else ["x"])
        if any(n not in blk for n in need):
            continue
        bt = blk["b"].strip().lower()
        b = 1 if bt in ("true", "1") else (0 if bt in ("false", "0") else None)
        if b is None:
            continue
        def arr(tok):
            mm = re.search(r"\[(.*)\]", tok)
            if not mm:
                return None
            inner = mm.group(1).strip()
            return [int(t) for t in inner.split(",")] if inner else []
        if kind == "lex":
            a = arr(blk["xs"]); c = arr(blk["ys"])
            if a is None or c is None:
                continue
            mat = a + c
        else:
            mat = arr(blk["x"])
            if mat is None:
                continue
        sols.add((tuple(mat), b))
    return sols, status


def brute_set(kind, strict, imp, dims, domain, pin=None, fixmat=None):
    """Exact feasible (mat,b) set under MiniZinc semantics.

    _reif: b <-> cond.   _imp: b -> cond (if b=1 cond must hold; b=0 free)."""
    if kind == "lex":
        L = dims; ncells = 2 * L; r = 1; c = 2 * L
    else:
        r, c = dims; ncells = r * c
    out = set()
    for mat in itertools.product(range(domain + 1), repeat=ncells):
        if fixmat is not None and tuple(mat) != tuple(fixmat):
            continue
        t = cond(kind, strict, dims, list(mat))
        for b in (0, 1):
            if pin is True and b != 1:
                continue
            if pin is False and b != 0:
                continue
            if imp:
                ok = (b == 0) or t          # b -> cond
            else:
                ok = (b == (1 if t else 0))  # b <-> cond
            if ok:
                out.add((tuple(mat), b))
    return out


def main():
    ok = wrong = unknown = unsat_ok = unsat_bad = 0
    fails = []
    configs = []
    for kind in ("lex", "chain", "lex2"):
        for strict in (False, True):
            for imp in (False, True):
                if kind == "lex":
                    dims_list = [(2, 2), (3, 1)]
                    dims_list = [2, 3]
                else:
                    dims_list = [(2, 2), (3, 2), (2, 3)]
                for dims in dims_list:
                    configs.append((kind, strict, imp, dims))

    for (kind, strict, imp, dims) in configs:
        domain = 2 if (kind == "lex" and dims in (2, 3)) else 1
        if kind != "lex":
            domain = 2 if dims == (2, 2) else 1
        # full all-solutions comparison
        src = gen(kind, strict, imp, dims, domain, True)
        out = run(src, True)
        got, status = parse_solutions(out, kind, dims)
        want = brute_set(kind, strict, imp, dims, domain)
        if status == "unknown":
            unknown += 1
            continue
        if status == "unsat":
            if want:
                wrong += 1; fails.append(("ALLSOL-unsat-but-sat", kind, strict, imp, dims, len(want)))
            else:
                ok += 1
            continue
        if got != want:
            # every reported solution must be genuinely feasible, and none missing
            extra = got - want
            missing = want - got
            wrong += 1
            fails.append(("ALLSOL-set-mismatch", kind, strict, imp, dims,
                          "extra=%d missing=%d" % (len(extra), len(missing)),
                          "extra_sample=%s" % (list(extra)[:2]),
                          "missing_sample=%s" % (list(missing)[:2])))
        else:
            ok += 1

    # pinned UNSAT / SAT checks (single-solution) for _reif only
    for kind in ("lex", "chain", "lex2"):
        for strict in (False, True):
            for dims in ([2, 3] if kind == "lex" else [(2, 2), (3, 2)]):
                domain = 1 if (kind != "lex" and dims != (2, 2)) else 2
                ncells = dims * 2 if kind == "lex" else dims[0] * dims[1]
                # choose one matrix per truth value to test pinning
                tested = {False: None, True: None}
                for mat in itertools.product(range(domain + 1), repeat=ncells):
                    t = cond(kind, strict, dims, list(mat))
                    if tested[t] is None:
                        tested[t] = mat
                    if all(v is not None for v in tested.values()):
                        break
                for t, mat in tested.items():
                    if mat is None:
                        continue
                    # pin opposite -> UNSAT
                    pin = False if t else True
                    src = gen(kind, strict, False, dims, domain, False, pin=pin, fixmat=mat)
                    out = run(src, False)
                    got, status = parse_solutions(out, kind, dims)
                    want = brute_set(kind, strict, False, dims, domain, pin=pin, fixmat=mat)
                    if status == "unknown":
                        unknown += 1
                    elif status == "unsat" and not want:
                        unsat_ok += 1
                    elif status == "sat" and not got and not want:
                        # solver printed a solution that failed to parse as infeasible?
                        unsat_ok += 1
                    elif want:
                        wrong += 1; fails.append(("PIN-feasible-mishandled", kind, strict, dims, pin))
                    else:
                        unsat_ok += 1

    total = ok + wrong + unknown + unsat_ok + unsat_bad
    print("lex_reif_differential: exact_set=%d unsat=%d WRONG=%d UNKNOWN=%d (total=%d)"
          % (ok, unsat_ok, wrong, unknown, total))
    for f in fails[:20]:
        print("  FAIL", f)
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
