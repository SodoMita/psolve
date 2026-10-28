#!/usr/bin/env python3
"""Brute-force differential verifier for FlatZinc SET VARIABLES.

MiniZinc emits `var set of U: S` decisions for value_precede_chain over
`var set of int`, regular_set, etc.  psolve encodes each set as ONE integer
bitmask variable pinned to |U| fresh 0/1 membership bits, and
``set_in_reif(k, S, r)`` / ``set_in(k, S)`` read/pin the corresponding bit.

This verifier generates small models in which EVERY decision variable is
marked `:: output_var` (otherwise FlatZinc solvers print nothing and a
solution-set comparison is meaningless), enumerates the exact brute-force
solution set over all subsets x all bool assignments, runs ``fznsolve -a``,
and requires exact equality of the two sets.  UNKNOWN is counted honestly
and is not a failure.

Usage: set_var_verify.py [N] [seed]
"""
import itertools
import os
import random
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FZ = os.path.join(ROOT, "fznsolve")
SCRATCH = os.path.join(ROOT, "scratch")

UNIVERSES = [
    ("range", (1, 3)), ("range", (0, 2)), ("range", (1, 2)),
    ("range", (0, 0)), ("literal", (2, 4, 6)), ("literal", (1, 3)),
]


def universe_text(ut, us):
    return f"{us[0]}..{us[1]}" if ut == "range" else "{" + ", ".join(map(str, us)) + "}"


def universe_vals(ut, us):
    return list(range(us[0], us[1] + 1)) if ut == "range" else list(us)


def parse_set_literal(txt):
    t = txt.strip()
    if t in ("{}", "[]", ""):
        return frozenset()
    if t.startswith("{") and t.endswith("}"):
        body = t[1:-1].strip()
        if not body:
            return frozenset()
        return frozenset(int(x.strip()) for x in body.split(",") if x.strip())
    m = re.fullmatch(r"\s*(-?\d+)\s*\.\.\s*(-?\d+)\s*", t)
    if m:
        lo, hi = int(m.group(1)), int(m.group(2))
        return frozenset(range(lo, hi + 1)) if lo <= hi else frozenset()
    # singleton scalar is also a valid one-element set display
    if re.fullmatch(r"-?\d+", t):
        return frozenset([int(t)])
    raise ValueError(f"bad set literal: {txt!r}")


def split_top(s, sep=","):
    out, cur, depth = [], [], 0
    for c in s:
        if c in "{[(":
            depth += 1
        elif c in "}])":
            depth -= 1
        if c == sep and depth == 0:
            out.append("".join(cur)); cur = []
        else:
            cur.append(c)
    if cur or out:
        out.append("".join(cur))
    return out


def parse_solution_block(block, set_names, bool_names):
    """Return a canonical tuple of (name, value) pairs, or None if no values."""
    vals = {}
    for m in re.finditer(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;\n]+);", block):
        name, raw = m.group(1), m.group(2).strip()
        if name == "x" and raw.startswith("array1d"):
            inner = re.search(r"\[(.*)\]", raw, re.S)
            if not inner:
                return None
            items = split_top(inner.group(1))
            for nm, item in zip(set_names, items):
                vals[nm] = parse_set_literal(item)
            continue
        if name in set_names:
            vals[name] = parse_set_literal(raw)
        elif name in bool_names:
            if raw in ("true", "false"):
                vals[name] = raw == "true"
            elif re.fullmatch(r"-?\d+", raw):
                vals[name] = int(raw) != 0
            else:
                return None
    if not vals:
        return None
    return tuple(sorted((k, v) for k, v in vals.items()))


def gen_instance(rng, force_unsat=False):
    nsets = rng.randint(1, 2)
    sets = []
    for i in range(nsets):
        ut, us = rng.choice(UNIVERSES)
        sets.append({"name": f"S{i}", "ut": ut, "us": us,
                     "vals": universe_vals(ut, us)})
    nbool = rng.randint(1, 2)
    bools = [f"b{i}" for i in range(nbool)]

    decls = []
    for s in sets:
        decls.append(f"var set of {universe_text(s['ut'], s['us'])}: {s['name']} :: output_var;")
    for b in bools:
        decls.append(f"var bool: {b} :: output_var;")

    cons, checks = [], []

    # set_in(k, S) -- forced membership
    if rng.random() < 0.55:
        s = rng.choice(sets)
        k = rng.choice(s["vals"])
        cons.append(f"constraint set_in({k}, {s['name']});")
        checks.append(lambda a, s=s, k=k: k in a[s["name"]])

    # set_in_reif(k, S, r) -- in or outside the universe
    for _ in range(rng.randint(1, 2)):
        s = rng.choice(sets)
        if rng.random() < 0.25:
            k = max(s["vals"]) + 7
        else:
            k = rng.choice(s["vals"])
        r = rng.choice(bools)
        cons.append(f"constraint set_in_reif({k}, {s['name']}, {r});")
        checks.append(lambda a, s=s, k=k, r=r: a[r] == (k in a[s["name"]]))

    # bool clause tying memberships together
    if rng.random() < 0.4:
        P = [rng.choice(bools)]
        N = [rng.choice(bools)]
        cons.append(f"constraint bool_clause([{P[0]}], [{N[0]}]);")
        checks.append(lambda a, P=P, N=N: a[P[0]] or (not a[N[0]]))

    # pin a bool
    if rng.random() < 0.35:
        b = rng.choice(bools)
        v = rng.choice([True, False])
        cons.append(f"constraint bool_eq({b}, {'true' if v else 'false'});")
        checks.append(lambda a, b=b, v=v: a[b] == v)

    if force_unsat:
        s = rng.choice(sets)
        k = rng.choice(s["vals"])
        b = bools[0]
        cons.append(f"constraint set_in({k}, {s['name']});")
        checks.append(lambda a, s=s, k=k: k in a[s["name"]])
        cons.append(f"constraint set_in_reif({k}, {s['name']}, {b});")
        checks.append(lambda a, s=s, k=k, b=b: a[b] == (k in a[s["name"]]))
        cons.append(f"constraint bool_eq({b}, false);")
        checks.append(lambda a, b=b: a[b] is False)

    cons.append("solve satisfy;")
    model = "\n".join(decls + cons) + "\n"

    # brute force over all subsets x all bool assignments
    domains = []
    for s in sets:
        subs = []
        for r in range(len(s["vals"]) + 1):
            for combo in itertools.combinations(s["vals"], r):
                subs.append(frozenset(combo))
        domains.append(subs)
    domains += [[False, True] for _ in bools]
    ref = set()
    for combo in itertools.product(*domains):
        a = {}
        for i, s in enumerate(sets):
            a[s["name"]] = combo[i]
        for j, b in enumerate(bools):
            a[b] = combo[len(sets) + j]
        if all(chk(a) for chk in checks):
            ref.add(tuple(sorted(a.items())))
    return model, ref


def run_fzn(model):
    os.makedirs(SCRATCH, exist_ok=True)
    fd, path = tempfile.mkstemp(suffix=".fzn", prefix="setvar_", dir=SCRATCH)
    try:
        os.write(fd, model.encode()); os.close(fd)
        p = subprocess.run([FZ, "-a", path], cwd=ROOT, capture_output=True, timeout=60)
        return p.stdout.decode(errors="replace") + p.stderr.decode(errors="replace")
    except subprocess.TimeoutExpired:
        return "TIMEOUT\n"
    finally:
        try: os.close(fd)
        except OSError: pass
        os.unlink(path)


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 80
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261028
    rng = random.Random(seed)
    ok = wrong = unknown = 0
    for i in range(N):
        force_unsat = (i % 13 == 0)
        model, ref = gen_instance(rng, force_unsat=force_unsat)
        out = run_fzn(model)
        set_names = re.findall(r"var set of [^:]+: (\w+)", model)
        bool_names = re.findall(r"var bool: (\w+)", model)
        if "=====UNKNOWN=====" in out:
            unknown += 1; continue
        if "=====UNSATISFIABLE=====" in out:
            got = set()
        else:
            got = set()
            for blk in out.split("----------"):
                if "==========" in blk or not blk.strip():
                    continue
                c = parse_solution_block(blk, set_names, bool_names)
                if c is not None:
                    got.add(c)
        if got == ref:
            ok += 1
        else:
            wrong += 1
            if wrong <= 5:
                print("--- MISMATCH ---\n" + model)
                print(f"ref={len(ref)} got={len(got)}")
                print("ref-only:", sorted(ref - got, key=str)[:3])
                print("got-only:", sorted(got - ref, key=str)[:3])
                print("out head:", out[:200].replace("\n", " | "))
    print(f"set_var_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown} (N={N}, seed={seed})")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
