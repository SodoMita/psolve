#!/usr/bin/env python3
"""Differential verifier for the exact bilinear int_times(x,y,z) encoding and
the variable-divisor int_div/int_mod encoding in the FlatZinc bridge
(src/fzn.c: encode_product / encode_divmod_var), against a brute-force
enumerator with exact MiniZinc semantics.

Pins from the psolve-dev channel:
  - int_times(x,y,z), z = 42, x in 1..5, y in 0..100  ->  -a must give exactly
    (1,42,42), (2,21,42), (3,14,42)  (no fractional or false solutions)
  - int_div/int_mod(a,b,q/r), a=7,  b=2  -> q=3,  r=1
  - int_div/int_mod(a,b,q/r), a=-7, b=2  -> q=-3, r=-1  (trunc toward zero)

Generated models randomly mix:
  - pure-int models (CP engine path) and models with a non-output float dummy
    var (forces the MIP bridge path where the new encodings live)
  - satisfy / minimize / maximize / -a enumeration
  - negative and zero-containing domains, affine operands (x + 1), squares
    (int_times(x,x,z)), divisors pinned to 0 (-> certified UNSAT)

Verdict contract checked:
  - UNKNOWN       -> always acceptable (honest), counted separately
  - UNSATISFIABLE -> brute force must find no witness
  - SAT assignment-> must satisfy every constraint exactly (z == x*y, or
                     b != 0 and q == trunc(a/b) and r == a - b*q)
  - -a            -> printed output tuples must equal the brute-force set
  - optimize      -> objective must equal the brute-force optimum

Usage: fzn_bilinear_verify.py [N] [seed]
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

FZ = os.environ.get("FZNSOLVE", "./fznsolve")


def mzn_divmod(a, b):
    """MiniZinc semantics: quotient truncates toward zero; the remainder, if
    non-zero, has the sign of the dividend (same as C's / and %)."""
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    r = a - b * q
    return q, r


def run_fzn(text, *options):
    fd, path = tempfile.mkstemp(suffix=".fzn")
    try:
        os.write(fd, text.encode())
        os.close(fd)
        p = subprocess.run([FZ, *options, path], capture_output=True, timeout=120)
        return p.stdout.decode()
    finally:
        try:
            os.close(fd)
        except OSError:
            pass
        os.unlink(path)


def parse_assignment(out):
    vals = {}
    for name, v in re.findall(r"^(\w+) = (-?\d+);", out, re.M):
        vals[name] = int(v)
    for name, _lo, _hi, xs in re.findall(
            r"^(\w+) = array1d\((-?\d+)\.\.(-?\d+), \[([^\]]*)\]\);", out, re.M):
        vals[name] = [int(t.strip()) for t in xs.split(",") if t.strip()]
    return vals


def parse_all_solutions(out, names):
    """Parse -a output into a set of tuples over `names` (blocks split on
    '----------')."""
    sols = set()
    for blk in re.split(r"^----------$", out, flags=re.M):
        vals = parse_assignment(blk)
        if all(n in vals for n in names):
            sols.add(tuple(vals[n] for n in names))
    return sols


def rng_domain(rng, hi=6, allow_zero=True):
    lo = rng.randint(-hi, hi if allow_zero else -1)
    up = rng.randint(lo, hi)
    if not allow_zero and lo <= 0 <= up:
        lo, up = rng.choice([(lo, -1), (1, up)]) if lo < 0 < up else ((1, up) if lo == 0 else (lo, -1))
    return lo, up


def gen_times(rng):
    """int_times model -> (text, names, witness_iter, opt_target)."""
    xlo, xhi = rng_domain(rng)
    ylo, yhi = rng_domain(rng)
    aexpr = "x"
    if rng.random() < 0.15:
        aexpr = "x"          # square case decided below
    square = rng.random() < 0.15
    if square:
        ylo, yhi = xlo, xhi
    affine = (not square) and rng.random() < 0.2
    if affine:
        aexpr = "x + 1"
    prods = [((a + 1) if affine else a) * b
             for a in range(xlo, xhi + 1) for b in range(ylo, yhi + 1)]
    zlo, zhi = min(prods), max(prods)
    if rng.random() < 0.3:   # sometimes narrow z to make tight/UNSAT models
        cut = rng.randint(zlo, zhi)
        if rng.random() < 0.5:
            zlo = cut
        else:
            zhi = cut
    force_mip = rng.random() < 0.5
    lines = ["predicate int_times(var int: a, var int: b, var int: c);"]
    lines.append(f"var {xlo}..{xhi}: x :: output_var;")
    lines.append(f"var {ylo}..{yhi}: y :: output_var;")
    lines.append(f"var {zlo}..{zhi}: z :: output_var;")
    if force_mip:
        lines.append("var 0.0..10.0: w;")
        lines.append("constraint float_lin_eq([1.0], [w], 1.5);")
    lines.append(f"constraint int_times({aexpr}, y, z);")
    checks = []
    roll = rng.random()
    if roll < 0.25:
        t = rng.randint(zlo, zhi)
        lines.append(f"constraint int_eq(z, {t});")
        checks.append(lambda v, t=t: v["z"] == t)
    elif roll < 0.4:
        t = rng.randint(zlo, zhi + 2)
        lines.append(f"constraint int_lin_ge([1], [z], {t});")
        checks.append(lambda v, t=t: v["z"] >= t)
    opt = None
    if rng.random() < 0.35:
        opt = rng.choice(["minimize", "maximize"])
        lines.append(f"solve {opt} z;")
    else:
        lines.append("solve satisfy;")
    text = "\n".join(lines) + "\n"

    def relation(x, y, z):
        lhs = (x + 1) if affine else x
        return z == lhs * y

    def witness_iter():
        for x in range(xlo, xhi + 1):
            for y in range(ylo, yhi + 1):
                for z in range(zlo, zhi + 1):
                    if not relation(x, y, z):
                        continue
                    v = {"x": x, "y": y, "z": z}
                    if all(c(v) for c in checks):
                        yield v

    return text, ["x", "y", "z"], witness_iter, (opt, "z")


def gen_divmod(rng):
    """int_div/int_mod with a VARIABLE divisor -> (text, names, wit, opt)."""
    alo, ahi = rng_domain(rng)
    if rng.random() < 0.15:
        blo = bhi = 0        # divisor pinned to 0 -> certified UNSAT
    else:
        blo, bhi = rng_domain(rng)
    qlo, qhi = rng_domain(rng, hi=12)
    rlo, rhi = rng_domain(rng, hi=12)
    force_mip = rng.random() < 0.5
    lines = ["predicate int_div(var int: a, var int: b, var int: q);",
             "predicate int_mod(var int: a, var int: b, var int: r);"]
    lines.append(f"var {alo}..{ahi}: a :: output_var;")
    lines.append(f"var {blo}..{bhi}: b :: output_var;")
    lines.append(f"var {qlo}..{qhi}: q :: output_var;")
    lines.append(f"var {rlo}..{rhi}: r :: output_var;")
    if force_mip:
        lines.append("var 0.0..10.0: w;")
        lines.append("constraint float_lin_eq([1.0], [w], 1.5);")
    lines.append("constraint int_div(a, b, q);")
    lines.append("constraint int_mod(a, b, r);")
    checks = []
    roll = rng.random()
    if roll < 0.25:
        t = rng.randint(alo, ahi)
        lines.append(f"constraint int_eq(a, {t});")
        checks.append(lambda v, t=t: v["a"] == t)
    elif roll < 0.4:
        t = rng.randint(blo, bhi)
        lines.append(f"constraint int_eq(b, {t});")
        checks.append(lambda v, t=t: v["b"] == t)
    opt = None
    tgt = None
    if rng.random() < 0.3:
        opt = rng.choice(["minimize", "maximize"])
        tgt = rng.choice(["q", "r", "a"])
        lines.append(f"solve {opt} {tgt};")
    else:
        lines.append("solve satisfy;")
    text = "\n".join(lines) + "\n"

    def witness_iter():
        for a in range(alo, ahi + 1):
            for b in range(blo, bhi + 1):
                if b == 0:
                    continue
                wq, wr = mzn_divmod(a, b)
                for q in range(qlo, qhi + 1):
                    if q != wq:
                        continue
                    for r in range(rlo, rhi + 1):
                        if r != wr:
                            continue
                        v = {"a": a, "b": b, "q": q, "r": r}
                        if all(c(v) for c in checks):
                            yield v

    return text, ["a", "b", "q", "r"], witness_iter, (opt, tgt)


def check_edge_pins():
    ok = wrong = 0

    def report(cond, label, out):
        nonlocal ok, wrong
        if cond:
            ok += 1
        else:
            wrong += 1
            print(f"WRONG[{label}]:\n{out}")

    # pin 1: channel task-1 acceptance test, -a, exact solution set (CP path)
    text = """predicate int_times(var int: a, var int: b, var int: c);
var 1..5: x :: output_var;
var 0..100: y :: output_var;
var 0..500: z :: output_var;
constraint int_times(x, y, z);
constraint int_eq(z, 42);
solve satisfy;
"""
    want = {(1, 42, 42), (2, 21, 42), (3, 14, 42)}
    out = run_fzn(text, "-a")
    report(parse_all_solutions(out, ["x", "y", "z"]) == want and "==========" in out,
           "pin times -a", out)
    # pin 1b: same model through the MIP bridge (non-output float dummy)
    text_mip = text.replace("solve satisfy;",
        "var 0.0..10.0: w;\nconstraint float_lin_eq([1.0], [w], 1.5);\nsolve satisfy;")
    out = run_fzn(text_mip, "-a")
    report(parse_all_solutions(out, ["x", "y", "z"]) == want and "==========" in out,
           "pin times -a MIP", out)
    # pins 2-4: div/mod with var divisor, trunc toward zero
    divtext = """predicate int_div(var int: a, var int: b, var int: q);
predicate int_mod(var int: a, var int: b, var int: r);
var -10..10: a :: output_var;
var -10..10: b :: output_var;
var -10..10: q :: output_var;
var -10..10: r :: output_var;
constraint int_div(a, b, q);
constraint int_mod(a, b, r);
constraint int_eq(a, %d);
constraint int_eq(b, %d);
solve satisfy;
"""
    for a, b, wq, wr in [(7, 2, 3, 1), (-7, 2, -3, -1), (7, -2, -3, 1), (-7, -2, 3, -1)]:
        out = run_fzn(divtext % (a, b))
        v = parse_assignment(out)
        report(v.get("q") == wq and v.get("r") == wr, f"pin div a={a} b={b}", out)
    # pin 5: divisor pinned to 0 -> constraint false -> certified UNSAT
    out = run_fzn((divtext % (5, 0)).replace("var -10..10: b :: output_var;",
                                             "var 0..0: b :: output_var;"))
    report("=====UNSATISFIABLE=====" in out, "pin div b=0 UNSAT", out)
    # pin 6: product fixed to a non-product (x pinned to 2, z=43 is odd)
    #        -> certified UNSAT
    textu = """predicate int_times(var int: a, var int: b, var int: c);
var 1..5: x :: output_var;
var 0..100: y :: output_var;
var 0..500: z :: output_var;
constraint int_times(x, y, z);
constraint int_eq(x, 2);
constraint int_eq(z, 43);
solve satisfy;
"""
    out = run_fzn(textu)
    report("=====UNSATISFIABLE=====" in out, "pin times z=43 UNSAT", out)
    # pin 7: square via MIP path, -a: x*x for x in -3..3
    texts = """predicate int_times(var int: a, var int: b, var int: c);
var -3..3: x :: output_var;
var -3..3: y :: output_var;
var -9..9: z :: output_var;
var 0.0..1.0: w;
constraint int_times(x, x, z);
constraint float_lin_eq([1.0], [w], 0.5);
solve satisfy;
"""
    out = run_fzn(texts, "-a")
    # y is unconstrained in this model, so -a enumerates it freely
    want = {(x, y, x * x) for x in range(-3, 4) for y in range(-3, 4)}
    report(parse_all_solutions(out, ["x", "y", "z"]) == want,
           "pin square -a MIP", out)
    return ok, wrong


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 250
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261101
    rng = random.Random(seed)
    ok = wrong = unknown = 0
    eok, ewrong = check_edge_pins()
    ok += eok
    wrong += ewrong
    for i in range(N):
        gen = gen_times if rng.random() < 0.5 else gen_divmod
        text, names, wit_iter, (opt, tgt) = gen(rng)
        witnesses = list(wit_iter())
        use_a = (opt is None) and rng.random() < 0.35
        out = run_fzn(text, "-a") if use_a else run_fzn(text)
        if "UNKNOWN" in out:
            unknown += 1
            continue
        if use_a:
            got = parse_all_solutions(out, names)
            want = {tuple(v[n] for n in names) for v in witnesses}
            if got == want and (want or "=====UNSATISFIABLE=====" in out):
                ok += 1
            else:
                wrong += 1
                print(f"WRONG[all-solutions] #{i}\n{text}\ngot {sorted(got)}\nwant {sorted(want)}\n{out}")
            continue
        if "UNSATISFIABLE" in out:
            if witnesses:
                wrong += 1
                print(f"WRONG[unsat-but-sat] #{i}\n{text}\nwitness {witnesses[0]}\n{out}")
            else:
                ok += 1
            continue
        vals = parse_assignment(out)
        if not all(n in vals for n in names) or vals not in witnesses:
            wrong += 1
            print(f"WRONG[bad-assignment] #{i}\n{text}\nparsed={vals}\n{out}")
            continue
        if opt == "minimize" and witnesses and vals[tgt] != min(v[tgt] for v in witnesses):
            wrong += 1
            print(f"WRONG[min-objective] #{i} got {vals[tgt]} want {min(v[tgt] for v in witnesses)}\n{text}\n{out}")
            continue
        if opt == "maximize" and witnesses and vals[tgt] != max(v[tgt] for v in witnesses):
            wrong += 1
            print(f"WRONG[max-objective] #{i} got {vals[tgt]} want {max(v[tgt] for v in witnesses)}\n{text}\n{out}")
            continue
        ok += 1
    print(f"fzn_bilinear_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown}  (N={N}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
