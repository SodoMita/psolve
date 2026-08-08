#!/usr/bin/env python3
"""Differential verifier for the FlatZinc int_div/int_mod/int_pow/set_in/
among handlers against a brute-force enumerator using exact MiniZinc
semantics (floor division, divisor-signed remainder, non-negative exponents).

Generates small random .fzn instances biased toward the edges that used to
lie (negative dividends, negative divisors, duplicate set members, inexact
powers) and checks ./fznsolve's verdict:

  - UNKNOWN        -> always acceptable (honest), counted separately
  - UNSATISFIABLE  -> brute force must find no lattice point
  - an assignment  -> must satisfy every constraint exactly; for optimize
                      models the objective must equal the brute-force optimum

Usage: divmod_verify.py [N] [seed]
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

FZ = "./fznsolve"


def floordiv_mod(a, k):
    """MiniZinc semantics: floor division, remainder sign of the divisor."""
    q = a // k          # Python // is floor division
    r = a - k * q       # sign follows divisor
    return q, r


def run_fzn(text):
    fd, path = tempfile.mkstemp(suffix=".fzn")
    try:
        os.write(fd, text.encode())
        os.close(fd)
        p = subprocess.run([FZ, path], capture_output=True, timeout=120)
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


def gen_instance(rng, unbounded_r=False):
    """Return (text, checks, assigns, witness_iter, opt) where checks are
    brute-force satisfiability predicates over a candidate assignment,
    assigns validate a printed assignment against the same MiniZinc
    semantics, witness_iter enumerates candidate assignments, and opt is
    None/'minimize'/'maximize' on x.  `unbounded_r` declares the result var
    as a bare `var int` (exercising the synthetic-box honesty downgrade);
    otherwise it gets an explicit box like real mzn2fzn output."""
    lines = ["var int: r :: output_var;" if unbounded_r else
             "var -40000..40000: r :: output_var;"]
    checks = []          # brute-force predicates over a full candidate dict
    assigns = []         # validators for the printed assignment (same rules)

    xlo, xhi = rng.choice([(-6, 6), (-4, 8), (0, 5), (-8, -2)])
    lines.append(f"var {xlo}..{xhi}: x :: output_var;")
    kind = rng.choice(["div", "mod", "pow", "set_in", "among"])

    if kind in ("div", "mod"):
        K = rng.choice([1, -1, 2, -2, 3, -3, 4, 5, -5])
        lines.append(f"constraint int_{kind}(x, {K}, r);")

        def derived(vals, K=K, kind=kind):
            q, rr = floordiv_mod(vals["x"], K)
            return q if kind == "div" else rr

        checks.append(lambda v: "r" in v and v["r"] == derived(v))
        assigns.append(lambda v: v.get("r") == derived(v))
    elif kind == "pow":
        exp = rng.choice([0, 1, 2, 3, 4, 5])
        lines.append(f"constraint int_pow(x, {exp}, r);")
        checks.append(lambda v, e=exp: "r" in v and v["r"] == v["x"] ** e)
        assigns.append(lambda v, e=exp: v.get("r") == v["x"] ** e)
    elif kind == "set_in":
        lines.pop(0)     # no r var here
        if rng.random() < 0.5:
            lo, hi = sorted((rng.randint(-8, 4), rng.randint(-4, 8)))
            S = f"{lo}..{hi}"
            members = set(range(lo, hi + 1))
        else:
            raw = [rng.randint(-8, 8) for _ in range(rng.randint(1, 6))]
            raw += raw[: rng.randint(0, 3)]      # duplicates on purpose
            rng.shuffle(raw)
            S = "{" + ", ".join(map(str, raw)) + "}"
            members = set(raw)
        lines.append(f"constraint set_in(x, {S});")
        checks.append(lambda v, m=frozenset(members): v["x"] in m)
        assigns.append(lambda v, m=frozenset(members): v.get("x") in m)
    elif kind == "among":
        lines.pop(0)     # no r var here
        raw = sorted(rng.sample(range(-5, 8), rng.randint(1, 4)))
        S = "{" + ", ".join(map(str, raw)) + "}"
        members = frozenset(raw)
        narr = rng.randint(2, 3)
        nwant = rng.randint(0, narr)
        lines.insert(0, f"array [1..{narr}] of var {xlo}..{xhi}: xs :: output_array([1..{narr}]);")
        lines.append(f"constraint fzn_among({nwant}, xs, {S});")

        def among_ok(vals, m=members, nwant=nwant):
            return sum(1 for t in vals["xs"] if t in m) == nwant

        checks.append(among_ok)
        assigns.append(lambda v: "xs" in v and among_ok(v))
    else:
        lines.pop(0)

    # optional extra relation on x to make instances tighter / infeasible
    roll = rng.random()
    if roll < 0.35:
        target = rng.randint(xlo, xhi)
        lines.append(f"constraint int_eq(x, {target});")
        checks.append(lambda v, t=target: v["x"] == t)
        assigns.append(lambda v, t=target: v.get("x") == t)
    elif roll < 0.55:
        t = rng.randint(xlo, xhi + 3)
        lines.append(f"constraint int_lin_ge([1], [x], {t});")
        checks.append(lambda v, t=t: v["x"] >= t)
        assigns.append(lambda v, t=t: v.get("x") >= t)
    elif roll < 0.7:
        t = rng.randint(xlo - 3, xhi)
        lines.append(f"constraint int_lin_le([1], [x], {t});")
        checks.append(lambda v, t=t: v["x"] <= t)
        assigns.append(lambda v, t=t: v.get("x") <= t)

    opt = None
    if rng.random() < 0.4:
        opt = rng.choice(["minimize", "maximize"])
        lines.append(f"solve {opt} x;")
    else:
        lines.append("solve satisfy;")
    text = "\n".join(lines) + "\n"

    have_r = kind in ("div", "mod", "pow")

    def witness_iter():
        xs_options = [None]
        if kind == "among":
            xs_options = list(itertools.product(range(xlo, xhi + 1), repeat=int(
                re.search(r"1\.\.(\d+)\] of var", text).group(1))))
        for x in range(xlo, xhi + 1):
            for xs in xs_options:
                v = {"x": x}
                if xs is not None:
                    v["xs"] = list(xs)
                if have_r:
                    if kind == "div":
                        v["r"] = floordiv_mod(x, K)[0]
                    elif kind == "mod":
                        v["r"] = floordiv_mod(x, K)[1]
                    else:
                        v["r"] = x ** exp
                    # the true model keeps r unbounded; the bounded-r variant
                    # declares ±40000, big enough for every generated value
                    if abs(v["r"]) > 40000 and not unbounded_r:
                        continue
                yield v

    return text, checks, assigns, witness_iter(), opt


EDGE_CASES = [
    # (fzn text, required substring in output) — fixed regressions
    # MiniZinc floor-division semantics: -7 div 3 = -3, -7 mod 3 = 2,
    # 7 mod -3 = -2 (previously C truncating division: -2 / -1 / wrong).
    ("var -10..10: x :: output_var;\nvar int: q :: output_var;\n"
     "constraint int_div(x, 3, q);\nconstraint int_eq(x, -7);\nsolve satisfy;\n",
     "q = -3;"),
    ("var -10..10: x :: output_var;\nvar int: q :: output_var;\n"
     "constraint int_div(x, -3, q);\nconstraint int_eq(x, 7);\nsolve satisfy;\n",
     "q = -3;"),
    ("var -10..10: x :: output_var;\nvar int: r :: output_var;\n"
     "constraint int_mod(x, 3, r);\nconstraint int_eq(x, -7);\nsolve satisfy;\n",
     "r = 2;"),
    ("var -10..10: x :: output_var;\nvar int: r :: output_var;\n"
     "constraint int_mod(x, -3, r);\nconstraint int_eq(x, 7);\nsolve satisfy;\n",
     "r = -2;"),
    # constant folding used C division too
    ("var int: q :: output_var;\nconstraint int_div(-7, 3, q);\nsolve satisfy;\n",
     "q = -3;"),
    ("var int: r :: output_var;\nconstraint int_mod(-7, 3, r);\nsolve satisfy;\n",
     "r = 2;"),
    # int_pow must not pin an inexact/overflowed double (previously a false
    # UNSATISFIABLE): 3^40 is a valid integer but > 2^53 -> honest UNKNOWN,
    # never UNSAT; 2^10 is exact -> 1024.
    ("var int: z :: output_var;\nconstraint int_pow(3, 40, z);\nsolve satisfy;\n",
     "UNKNOWN"),
    ("var int: z :: output_var;\nconstraint int_pow(2, 10, z);\nsolve satisfy;\n",
     "z = 1024;"),
    # set_in must not truncate enumerations beyond the internal cap
    # (280 values > old silent 256 cap; 265 is a member -> SAT).
    ("var 1..300: y :: output_var;\nconstraint int_eq(y, 265);\n"
     "constraint set_in(y, {" + ",".join(str(v) for v in range(1, 281)) + "});\nsolve satisfy;\n",
     "y = 265;"),
    # duplicate set members must not double-count in among
    ("array [1..2] of var 1..4: xs :: output_array([1..2]);\n"
     "constraint fzn_among(2, xs, {2, 2, 3, 3});\nsolve satisfy;\n",
     "xs = array1d(1..2, ["),
    # unbounded var with the only solutions outside the synthetic box:
    # false UNSATISFIABLE previously (z = 2e10 is a valid integer point).
    ("var int: z :: output_var;\nconstraint int_lin_eq([1], [z], 20000000000);\nsolve satisfy;\n",
     "UNKNOWN"),
    # count over a variable target: honest UNKNOWN (used to count zeros)
    ("array [1..3] of var 1..3: x :: output_array([1..3]);\nvar 1..3: y :: output_var;\n"
     "var 0..3: c :: output_var;\nconstraint int_eq(c, 2);\n"
     "constraint fzn_count_eq(x, y, c);\nconstraint int_eq(y, 3);\nsolve satisfy;\n",
     "UNKNOWN"),
]


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260808
    rng = random.Random(seed)
    ok = wrong = unknown = 0
    for idx, (text, want) in enumerate(EDGE_CASES):
        out = run_fzn(text)
        if want in out:
            ok += 1
        elif "UNKNOWN" in out and want == "UNKNOWN":
            ok += 1
        else:
            wrong += 1
            print(f"WRONG[edge] #{idx}: want '{want}'\n{text}\n{out}")
    for i in range(N):
        # 20% unbounded `var int: r` — exercises the honesty downgrade; the
        # rest use a bounded decl like real mzn2fzn output (stronger signal).
        text, checks, assigns, wit_iter, opt = gen_instance(rng, unbounded_r=(i % 5 == 0))
        witnesses = [v["x"] for v in wit_iter if all(c(v) for c in checks)]

        out = run_fzn(text)
        if "UNKNOWN" in out:
            unknown += 1
            continue
        if "UNSATISFIABLE" in out:
            if witnesses:
                wrong += 1
                print(f"WRONG[unsat-but-sat] #{i}\n{text}\nwitness x={witnesses[0]}\n{out}")
            else:
                ok += 1
            continue
        vals = parse_assignment(out)
        if "x" not in vals or not all(a(vals) for a in assigns):
            wrong += 1
            print(f"WRONG[bad-assignment] #{i}\n{text}\nparsed={vals}\n{out}")
            continue
        if opt == "minimize" and (not witnesses or vals["x"] != min(witnesses)):
            wrong += 1
            print(f"WRONG[min-objective] #{i} got {vals['x']} want {min(witnesses) if witnesses else None}\n{text}\n{out}")
            continue
        if opt == "maximize" and (not witnesses or vals["x"] != max(witnesses)):
            wrong += 1
            print(f"WRONG[max-objective] #{i} got {vals['x']} want {max(witnesses) if witnesses else None}\n{text}\n{out}")
            continue
        ok += 1
    print(f"divmod_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown}  (N={N}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
