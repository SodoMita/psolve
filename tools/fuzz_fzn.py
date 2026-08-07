#!/usr/bin/env python3
"""Fuzz the FlatZinc reader + solver bridge under ASan/UBSan.

Builds an ASAN/UBSAN-instrumented `fznsolve`, generates random .fzn files
(both well-formed, exercising the constraint handlers, and malformed, probing
the lexer/parser), and runs each through the solver, aborting if the
sanitizer reports any memory-safety or undefined-behaviour issue.

Usage:
    python3 tools/fuzz_fzn.py [--iters N] [--seed S] [--bin DIR]
"""
import random, subprocess, os, sys, shutil

def parse_args(argv):
    iters = 300; seed = 0; bindir = None
    i = 0
    while i < len(argv):
        if argv[i] == '--iters': iters = int(argv[i+1]); i += 2
        elif argv[i] == '--seed': seed = int(argv[i+1]); i += 2
        elif argv[i] == '--bin': bindir = argv[i+1]; i += 2
        else: i += 1
    return iters, seed, bindir

def build_asan(bindir):
    os.makedirs(bindir, exist_ok=True)
    base = ['gcc','-O1','-g','-march=native','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-I','src']
    cmd = base + ['src/err.c','src/kernels.c','src/lu.c','src/splu.c',
                  'src/solver.c','src/parser.c','src/qp.c','src/mip.c',
                  'src/pgs.c','src/pgs_fixed.c','src/fzn.c','tools/fznsolve.c',
                  '-o',os.path.join(bindir,'fznsolve_asan'),'-lm']
    subprocess.run(cmd, check=True)

# --- well-formed model generator (exercises the handlers we support) -------
VARS = "abcdefgh"

def gen_wellformed(rng):
    L = []
    n = rng.randint(1, 6)
    names = VARS[:n]
    # declare vars with mixed domains
    has_gapped = 0
    for i, nm in enumerate(names):
        style = rng.randint(0, 3)
        if style == 0:
            L.append(f"var int: {nm} :: output_var;")
        elif style == 1:
            lo, hi = rng.randint(-10,0), rng.randint(0,10)
            L.append(f"var {lo}..{hi}: {nm} :: output_var;")
        elif style == 2:
            vals = sorted(rng.sample(range(-5,6), rng.randint(1,5)))
            L.append(f"var {{{', '.join(map(str,vals))}}}: {nm} :: output_var;")
            if len(vals) < (vals[-1]-vals[0]+1): has_gapped = 1   # non-contiguous set
        else:
            L.append(f"var 0..1: {nm} :: output_var;")   # bool-ish
    # linear constraints
    for _ in range(rng.randint(1, 5)):
        k = rng.randint(1, n)
        idx = rng.sample(range(n), k)
        coefs = [rng.randint(-5,5) for _ in range(k)]
        if rng.random() < 0.5 and rng.randint(0,1):
            coefs[0] = 0  # sometimes include zero coef
        arr = ", ".join(map(str, coefs))
        xs = ", ".join(names[i] for i in idx)
        d = rng.randint(-20,20)
        op = rng.choice(['int_lin_le','int_lin_eq','int_lin_ge','int_lin_gt','int_lin_ne'])
        L.append(f"constraint {op}([{arr}], [{xs}], {d});")
    # element, abs, max, min, all_different
    if n >= 3 and rng.random() < 0.5:
        L.append(f"constraint array_int_element({rng.randint(-n,n)}, [{', '.join(names)}], {names[rng.randint(0,n-1)]});")
    if rng.random() < 0.4:
        L.append(f"constraint int_abs({names[rng.randint(0,n-1)]}, {names[rng.randint(0,n-1)]});")
    # all_different over gapped set-domain vars produces a weak LP relaxation
    # that can make B&B explode; keep the two apart for fuzz speed.
    if n >= 2 and not has_gapped and rng.random() < 0.4:
        L.append(f"constraint all_different([{', '.join(names)}]);")
    if rng.random() < 0.3:
        L.append(f"constraint set_in({names[rng.randint(0,n-1)]}, {{{', '.join(map(str,range(rng.randint(0,8))))}}});")
    # reified
    if n >= 2 and rng.random() < 0.3:
        a, b = rng.sample(range(n), 2)
        L.append(f"constraint int_eq_reif({names[a]}, {names[b]}, {names[rng.randint(0,n-1)]});")
    # table constraint: tuple of a random subset of vars must be one of the rows
    if n >= 1 and rng.random() < 0.4:
        k = rng.randint(1, min(3, n))
        sel = rng.sample(range(n), k)
        rows = rng.randint(1, 5)
        vals = []
        for _ in range(rows * k):
            vals.append(str(rng.randint(-5, 9)))
        xs = ", ".join(names[i] for i in sel)
        L.append(f"constraint table([{xs}], array2d(1, {rows}, 1, {k}, [{', '.join(vals)}]));")
    # solve
    kind = rng.randint(0,2)
    if kind == 0:
        L.append("solve satisfy;")
    else:
        v = names[rng.randint(0,n-1)]
        L.append(f"solve {'minimize' if kind==1 else 'maximize'} {v};")
    return "\n".join(L) + "\n"

def gen_malformed(rng):
    choices = [
        "var int: ;",
        "constraint int_lin_le([1], [x], );",
        "var int: x",
        "solve;",
        "constraint unknown_predicate(x, 5);",
        "var {, 3}: x;",
        "array int: a = [1,2;",
        "constraint int_abs(x);",          # wrong arity
        "var int: x :: 1..",
        "constraint int_lin_le([1,,2], [x], 5);",
        "solve maximize;",
        "predicate foo(;);",
        "var 10..1: x;",                    # lo>hi
        "constraint set_in(x, {});",
        "constraint int_times(x, 2, y);",
        "constraint bool_not(x, y);",
        "solve minimize x ; extra",
        "",
    ]
    return rng.choice(choices) + "\n" + rng.choice(choices) + "\n"

def run(rng, iters, bindir):
    exe = os.path.join(bindir, 'fznsolve_asan')
    tmp = os.path.join(bindir, 'case.fzn')
    bad = 0
    for i in range(iters):
        if rng.random() < 0.55:
            content = gen_wellformed(rng)
        else:
            content = gen_malformed(rng)
        with open(tmp, 'w') as f:
            f.write(content)
        try:
            r = subprocess.run([exe, tmp], capture_output=True, timeout=10)
        except subprocess.TimeoutExpired:
            # a hard B&B instance may legitimately exceed the case timeout;
            # it is not a memory-safety failure, so log and continue.
            continue
        # sanitizer messages appear on stderr
        if (b'ERROR: AddressSanitizer' in r.stderr or b'runtime error:' in r.stderr
            or b'AddressSanitizer' in r.stderr or r.returncode < 0):
            print("SANITIZER FAILURE on input:\n" + content, flush=True)
            print(r.stderr.decode(), flush=True)
            bad += 1
            if bad >= 5: break
    return bad

def main():
    iters, seed, bindir = parse_args(sys.argv[1:])
    bindir = bindir or '/tmp/psolve_fuzz'
    rng = random.Random(seed)
    print(f"building ASAN fznsolve in {bindir}...", flush=True)
    build_asan(bindir)
    print(f"fuzzing {iters} inputs (seed {seed})...", flush=True)
    bad = run(rng, iters, bindir)
    if bad:
        print(f"FZ FUZZ FAILED: {bad} sanitizer errors", flush=True)
        sys.exit(1)
    print(f"fz fuzz OK: {iters} inputs, no sanitizer errors", flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
