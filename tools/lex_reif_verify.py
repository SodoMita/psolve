#!/usr/bin/env python3
"""Differential verifier for psolve's native reified-lex FlatZinc predicates."""
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLVER = os.path.join(ROOT, "fznsolve")
SCRATCH = os.path.join(ROOT, "scratch")
DOMAIN = (0, 2)


def lex_le(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] < b[i]:
            return True
        if a[i] > b[i]:
            return False
    return len(a) <= len(b)


def chain_ok(mat, r, c):
    for cc in range(c - 1):
        col1 = [mat[rr * c + cc] for rr in range(r)]
        col2 = [mat[rr * c + cc + 1] for rr in range(r)]
        if not lex_le(col1, col2):
            return False
    return True


def lex2_ok(mat, r, c):
    for rr in range(r - 1):
        if not lex_le(mat[rr * c:(rr + 1) * c], mat[(rr + 1) * c:(rr + 2) * c]):
            return False
    for cc in range(c - 1):
        col1 = [mat[rr * c + cc] for rr in range(r)]
        col2 = [mat[rr * c + cc + 1] for rr in range(r)]
        if not lex_le(col1, col2):
            return False
    return True


def cond_of(kind, dims, mat_or_pair):
    if kind == "lex":
        xs, ys = mat_or_pair
        return lex_le(xs, ys)
    if kind == "chain":
        r, c = dims
        return chain_ok(mat_or_pair, r, c)
    r, c = dims
    return lex2_ok(mat_or_pair, r, c)


def brute_feasible(kind, dims, fixed, pin):
    if kind == "lex":
        L = dims
        nvars = 2 * L + 1  # xs, ys, b
        # layout: xs 0..L-1, ys L..2L-1, b=2L
        bidx = 2 * L
        def get_mat(assign):
            xs = [assign[i] for i in range(L)]
            ys = [assign[L + i] for i in range(L)]
            return xs, ys
    else:
        r, c = dims
        ncells = r * c
        bidx = ncells
        nvars = ncells + 1
        def get_mat(assign):
            return [assign[i] for i in range(ncells)]

    lo, hi = DOMAIN
    for assign in itertools.product(range(lo, hi + 1), repeat=nvars):
        # fixed cells
        ok = True
        for key, val in fixed.items():
            # key: (name, 1-based flat index)
            name, idx1 = key
            pos = None
            if kind == "lex":
                if name == "xs":
                    pos = idx1 - 1
                elif name == "ys":
                    pos = L + (idx1 - 1)
            else:
                pos = idx1 - 1
            if pos is not None and assign[pos] != val:
                ok = False
                break
        if not ok:
            continue
        b = assign[bidx]
        if pin == "true" and b != 1:
            continue
        if pin == "false" and b != 0:
            continue
        mat = get_mat(assign)
        truth = cond_of(kind, dims, mat if kind != "lex" else mat)
        # mat for lex is (xs,ys) tuple already? adjust
        if kind == "lex":
            xs, ys = mat
            truth = lex_le(xs, ys)
        if b == (1 if truth else 0):
            return True
    return False


import itertools

def gen_source(kind, dims, fixed, pin, mode):
    lines = []
    if kind == "lex":
        L = dims
        lines.append("predicate fzn_lex_lesseq_int_reif(array[int] of var int: xs, array[int] of var int: ys, var bool: b);")
        lines.append(f"array [1..{L}] of var {DOMAIN[0]}..{DOMAIN[1]}: xs :: output_array([1..{L}]);")
        lines.append(f"array [1..{L}] of var {DOMAIN[0]}..{DOMAIN[1]}: ys :: output_array([1..{L}]);")
        lines.append("var bool: b :: output_var;")
        lines.append("constraint fzn_lex_lesseq_int_reif(xs,ys,b);")
    elif kind == "chain":
        r, c = dims
        lines.append("predicate fzn_lex_chain_lesseq_int_reif(array[int,int] of var int: xs, var bool: b);")
        lines.append(f"array [1..{r},1..{c}] of var {DOMAIN[0]}..{DOMAIN[1]}: x :: output_array([1..{r},1..{c}]);")
        lines.append("var bool: b :: output_var;")
        lines.append("constraint fzn_lex_chain_lesseq_int_reif(x,b);")
    else:
        r, c = dims
        lines.append("predicate fzn_lex2_reif(array[int,int] of var int: xs, var bool: b);")
        lines.append(f"array [1..{r},1..{c}] of var {DOMAIN[0]}..{DOMAIN[1]}: x :: output_array([1..{r},1..{c}]);")
        lines.append("var bool: b :: output_var;")
        lines.append("constraint fzn_lex2_reif(x,b);")

    # fixed cells
    for key, val in fixed.items():
        name, idx1 = key
        if kind == "lex":
            lines.append(f"constraint int_eq({name}[{idx1}], {val});")
        else:
            lines.append(f"constraint int_eq(x[{idx1}], {val});")

    if pin == "true":
        lines.append("constraint bool_eq(b, true);")
    elif pin == "false":
        lines.append("constraint bool_eq(b, false);")

    lines.append("solve satisfy;")
    return "\n".join(lines) + "\n"


def parse_output(stdout):
    blocks = []
    cur = []
    markers = []
    for ln in stdout.splitlines():
        s = ln.strip()
        if s == "----------":
            blocks.append(cur)
            cur = []
        elif s in ("==========", "=====UNSATISFIABLE=====", "=====UNKNOWN====="):
            markers.append(s)
        elif s == "" or s.startswith("%"):
            pass
        else:
            cur.append(ln)
    if cur:
        blocks.append(cur)
    return blocks, markers


def parse_block(block, kind, dims):
    vals = {}
    for ln in block:
        ln = ln.strip()
        if not ln:
            continue
        m = re.match(r"^([A-Za-z_]\w*)\s*=\s*(.*);$", ln)
        if not m:
            return None
        name, rhs = m.group(1), m.group(2)
        am = re.match(r"^array1d\((\d+)\.\.(\d+),\s*\[(.*)\]\)$", rhs)
        if am:
            toks = [t.strip() for t in am.group(3).split(",")] if am.group(3).strip() != "" else []
            vals[name] = ("arr", int(am.group(1)), int(am.group(2)), toks)
        else:
            vals[name] = ("scalar", rhs.strip())
    if kind == "lex":
        L = dims
        if "xs" not in vals or "ys" not in vals or "b" not in vals:
            return None
        if vals["xs"][0] != "arr" or vals["ys"][0] != "arr":
            return None
        try:
            xs = [int(t) for t in vals["xs"][3]]
            ys = [int(t) for t in vals["ys"][3]]
        except ValueError:
            return None
        btoken = vals["b"][1].lower()
        if btoken == "true":
            b = 1
        elif btoken == "false":
            b = 0
        else:
            try:
                b = int(btoken)
            except ValueError:
                return None
        if len(xs) != L or len(ys) != L:
            return None
        return ("lex", xs, ys, b)
    else:
        r, c = dims
        if "x" not in vals or "b" not in vals:
            return None
        if vals["x"][0] != "arr":
            return None
        try:
            mat = [int(t) for t in vals["x"][3]]
        except ValueError:
            return None
        btoken = vals["b"][1].lower()
        if btoken == "true":
            b = 1
        elif btoken == "false":
            b = 0
        else:
            try:
                b = int(btoken)
            except ValueError:
                return None
        if len(mat) != r * c:
            return None
        return ("mat", mat, b)


def verify_solution(kind, dims, fixed, pin, sol):
    if sol is None:
        return False
    if kind == "lex":
        _, xs, ys, b = sol
        mat_for_cond = (xs, ys)
        # domain check
        for v in xs + ys:
            if v < DOMAIN[0] or v > DOMAIN[1]:
                return False
        # fixed check
        L = dims
        for key, val in fixed.items():
            name, idx1 = key
            arr = xs if name == "xs" else ys
            if arr[idx1 - 1] != val:
                return False
    else:
        _, mat, b = sol
        r, c = dims
        for v in mat:
            if v < DOMAIN[0] or v > DOMAIN[1]:
                return False
        for key, val in fixed.items():
            name, idx1 = key
            if mat[idx1 - 1] != val:
                return False
        mat_for_cond = mat

    # pin check
    if pin == "true" and b != 1:
        return False
    if pin == "false" and b != 0:
        return False

    # semantics
    if kind == "lex":
        xs, ys = mat_for_cond
        truth = lex_le(xs, ys)
    elif kind == "chain":
        r, c = dims
        truth = chain_ok(mat_for_cond, r, c)
    else:
        r, c = dims
        truth = lex2_ok(mat_for_cond, r, c)
    return b == (1 if truth else 0)


def run_one(source):
    fd, path = tempfile.mkstemp(suffix=".fzn", dir=SCRATCH)
    try:
        with os.fdopen(fd, "w") as f:
            f.write(source)
        r = subprocess.run([SOLVER, path], capture_output=True, text=True, timeout=60)
        return r.stdout, r.stderr, r.returncode
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def main():
    rng = random.Random(20261027)
    ok = wrong = unknown = 0
    tests = 0

    kinds = ["lex", "chain", "lex2"]

    # Random models
    N = 40
    for kind in kinds:
        for _ in range(N):
            if kind == "lex":
                dims = rng.choice([2, 3])
            else:
                dims = rng.choice([(2, 2), (3, 2)])
            pin = rng.choice([None, "true", "false"])
            fixed = {}
            mode = "single"
            # occasionally enumerate for small 2x2
            if kind != "lex" and dims == (2, 2) and rng.random() < 0.15:
                mode = "all"
            source = gen_source(kind, dims, fixed, pin, mode)
            feas = brute_feasible(kind, dims, fixed, pin)
            stdout, stderr, rc = run_one(source)
            tests += 1
            blocks, markers = parse_output(stdout)
            if rc != 0:
                wrong += 1
                continue
            if "=====UNSATISFIABLE=====" in markers:
                if feas:
                    wrong += 1
                else:
                    ok += 1
                continue
            if "=====UNKNOWN=====" in markers:
                unknown += 1
                continue
            if not blocks:
                wrong += 1
                continue
            # if expecting UNSAT but got solutions
            if not feas:
                wrong += len(blocks) if blocks else 1
                continue
            for blk in blocks:
                sol = parse_block(blk, kind, dims)
                if verify_solution(kind, dims, fixed, pin, sol):
                    ok += 1
                else:
                    wrong += 1

    # UNSAT fixed-matrix cases
    for kind in kinds:
        for _ in range(12):
            if kind == "lex":
                dims = rng.choice([2, 3])
            else:
                dims = rng.choice([(2, 2), (3, 2)])
            # random matrix
            if kind == "lex":
                L = dims
                xs = [rng.randint(*DOMAIN) for _ in range(L)]
                ys = [rng.randint(*DOMAIN) for _ in range(L)]
                truth = lex_le(xs, ys)
                fixed = {}
                for i, v in enumerate(xs, start=1):
                    fixed[("xs", i)] = v
                for i, v in enumerate(ys, start=1):
                    fixed[("ys", i)] = v
                mat_for_cond = (xs, ys)
            else:
                r, c = dims
                mat = [rng.randint(*DOMAIN) for _ in range(r * c)]
                truth = chain_ok(mat, r, c) if kind == "chain" else lex2_ok(mat, r, c)
                fixed = {("x", i + 1): mat[i] for i in range(len(mat))}
                mat_for_cond = mat

            # pin opposite to truth -> UNSAT
            pin = "false" if truth else "true"
            source = gen_source(kind, dims, fixed, pin, "single")
            feas = brute_feasible(kind, dims, fixed, pin)
            stdout, stderr, rc = run_one(source)
            tests += 1
            blocks, markers = parse_output(stdout)
            if rc != 0:
                wrong += 1
                continue
            if "=====UNSATISFIABLE=====" in markers:
                if feas:
                    wrong += 1
                else:
                    ok += 1
                continue
            if "=====UNKNOWN=====" in markers:
                unknown += 1
                continue
            if not feas:
                wrong += len(blocks) if blocks else 1
            else:
                for blk in blocks:
                    sol = parse_block(blk, kind, dims)
                    if verify_solution(kind, dims, fixed, pin, sol):
                        ok += 1
                    else:
                        wrong += 1

    # SAT fixed-matrix cases (pin matching truth)
    for kind in kinds:
        for _ in range(8):
            if kind == "lex":
                dims = rng.choice([2, 3])
                L = dims
                xs = [rng.randint(*DOMAIN) for _ in range(L)]
                ys = [rng.randint(*DOMAIN) for _ in range(L)]
                truth = lex_le(xs, ys)
                fixed = {}
                for i, v in enumerate(xs, start=1):
                    fixed[("xs", i)] = v
                for i, v in enumerate(ys, start=1):
                    fixed[("ys", i)] = v
            else:
                dims = rng.choice([(2, 2), (3, 2)])
                r, c = dims
                mat = [rng.randint(*DOMAIN) for _ in range(r * c)]
                truth = chain_ok(mat, r, c) if kind == "chain" else lex2_ok(mat, r, c)
                fixed = {("x", i + 1): mat[i] for i in range(len(mat))}
            pin = "true" if truth else "false"
            source = gen_source(kind, dims, fixed, pin, "single")
            feas = brute_feasible(kind, dims, fixed, pin)
            stdout, stderr, rc = run_one(source)
            tests += 1
            blocks, markers = parse_output(stdout)
            if rc != 0:
                wrong += 1
                continue
            if "=====UNSATISFIABLE=====" in markers:
                if feas:
                    wrong += 1
                else:
                    ok += 1
                continue
            if "=====UNKNOWN=====" in markers:
                unknown += 1
                continue
            if not blocks:
                wrong += 1
                continue
            for blk in blocks:
                sol = parse_block(blk, kind, dims)
                if verify_solution(kind, dims, fixed, pin, sol):
                    ok += 1
                else:
                    wrong += 1

    print(f"lex_reif_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown}")


if __name__ == "__main__":
    main()
