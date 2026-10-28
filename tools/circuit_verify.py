#!/usr/bin/env python3
"""Brute-force differential verifier for the FlatZinc circuit handler.

Exercises the native `fzn_circuit` / `gecode_circuit` CP propagator with
subtour elimination.  For small n we enumerate ALL Hamiltonian successor
cycles exactly, apply the same extra constraints as the generated model, and
compare the complete `-a` solution set against ./fznsolve.  This catches both
wrong SAT answers (a proper subtour accepted) and missing solutions (over-
aggressive pruning).  Also covers UNSAT models and n up to 12 without -a.

Usage: circuit_verify.py [N] [seed]
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


def hamiltonian_cycles(n, off=1):
    """All successor arrays forming ONE cycle over nodes 0..n-1.

    Fix node 0's position and permute the rest: every permutation of
    1..n-1 gives exactly one cycle.  Returns list of tuples of values
    (off-based node labels)."""
    out = []
    for perm in itertools.permutations(range(1, n)):
        order = (0,) + perm
        succ = [0] * n
        for i, node in enumerate(order):
            nxt = order[(i + 1) % n]
            succ[node] = nxt + off
        out.append(tuple(succ))
    return out


def run_fzn(model, all_solutions=True, timeout=90):
    os.makedirs(SCRATCH, exist_ok=True)
    fd, path = tempfile.mkstemp(suffix=".fzn", prefix="circuit_", dir=SCRATCH)
    try:
        os.write(fd, model.encode()); os.close(fd)
        cmd = [FZ] + (["-a"] if all_solutions else []) + [path]
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=timeout)
        return p.stdout.decode(errors="replace") + p.stderr.decode(errors="replace")
    except subprocess.TimeoutExpired:
        return "TIMEOUT\n"
    finally:
        try: os.close(fd)
        except OSError: pass
        os.unlink(path)


def parse_solutions(out):
    sols = []
    for blk in out.split("----------"):
        m = re.search(r"^\s*succ\s*=\s*array1d\([^,]+,\s*\[([^\]]*)\]\)", blk, re.M)
        if m:
            vals = tuple(int(x.strip()) for x in m.group(1).split(",") if x.strip())
            sols.append(vals)
    return set(sols)


def gen_instance(rng, n, offset=1, use_gecode=False):
    """Return (model_text, expected_set_or_None).  None => too big to brute force."""
    lines = []
    for i in range(1, n + 1):
        lines.append(f"var {offset}..{offset+n-1}: X{i};")
    lines.append("array [1..%d] of var %d..%d: succ :: output_array([1..%d]) = [%s];" %
                 (n, offset, offset + n - 1, n, ",".join(f"X{i}" for i in range(1, n + 1))))

    extras = []   # list of (text, predicate_on_succ_tuple)
    # Pin one successor to a chosen value (keeps at least one solution usually)
    if rng.random() < 0.6:
        i = rng.randint(1, n)
        v = rng.randint(offset, offset + n - 1)
        extras.append((f"constraint int_eq(X{i}, {v});",
                       lambda s, i=i, v=v: s[i - 1] == v))
    # Forbid one successor value
    if rng.random() < 0.4:
        i = rng.randint(1, n)
        v = rng.randint(offset, offset + n - 1)
        extras.append((f"constraint int_ne(X{i}, {v});",
                       lambda s, i=i, v=v: s[i - 1] != v))
    # An inequality on a successor
    if rng.random() < 0.3:
        i = rng.randint(1, n)
        v = rng.randint(offset, offset + n - 1)
        extras.append((f"constraint int_le(X{i}, {v});",
                       lambda s, i=i, v=v: s[i - 1] <= v))

    if use_gecode:
        lines.append(f"constraint gecode_circuit({offset}, succ);")
    else:
        lines.append("constraint fzn_circuit(succ);")
    for txt, _ in extras:
        lines.append(txt)
    lines.append("solve satisfy;")
    model = "\n".join(lines) + "\n"

    if n <= 8:
        ref = set()
        for cyc in hamiltonian_cycles(n, offset):
            if all(pred(cyc) for _, pred in extras):
                ref.add(cyc)
        return model, ref
    return model, None


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20261028
    rng = random.Random(seed)
    ok = wrong = unknown = large_ok = 0
    for i in range(N):
        n = rng.choice([3, 4, 5, 6]) if i % 5 else rng.choice([7, 8])
        use_gecode = (rng.random() < 0.3)
        # `fzn_circuit` is 1-based by MiniZinc convention (values name the
        # 1..n array indices).  A 0-based node labelling is only expressible
        # as `gecode_circuit(0, x)`; feeding 0-based values to fzn_circuit is
        # a semantically invalid model, so never generate that combination.
        off = rng.choice([0, 1]) if use_gecode else 1
        model, ref = gen_instance(rng, n, offset=off, use_gecode=use_gecode)
        out = run_fzn(model, all_solutions=True)
        if "=====UNKNOWN=====" in out:
            unknown += 1; continue
        got = parse_solutions(out)
        if "=====UNSATISFIABLE=====" in out:
            got = set()
        if ref is None:
            # only check every printed solution is a valid Hamiltonian cycle
            valid = hamiltonian_cycles(n, off)
            if all(s in set(valid) for s in got):
                large_ok += 1
            else:
                wrong += 1
            continue
        if got == ref:
            ok += 1
        else:
            wrong += 1
            if wrong <= 5:
                print("--- MISMATCH ---\n" + model)
                print(f"ref={len(ref)} got={len(got)}")
                print("missing:", sorted(ref - got)[:3])
                print("extra:", sorted(got - ref)[:3])
                print("out:", out[:250].replace("\n", " | "))
    # Large-n scaling (no -a, just SAT): must solve n=70 quickly.
    large = []
    for n in (16, 32, 64, 70):
        lines = [f"var 1..{n}: X{i};" for i in range(1, n + 1)]
        lines.append("array [1..%d] of var 1..%d: succ :: output_array([1..%d]) = [%s];" %
                     (n, n, n, ",".join(f"X{i}" for i in range(1, n + 1))))
        lines.append("constraint fzn_circuit(succ);")
        lines.append("solve satisfy;")
        out = run_fzn("\n".join(lines) + "\n", all_solutions=False, timeout=30)
        good = ("TIMEOUT" not in out and "UNKNOWN" not in out and
                parse_solutions(out.replace("----------", "----------")))
        # validate the printed successor array is a genuine single cycle
        s = next(iter(parse_solutions(out)), None) if "UNKNOWN" not in out else None
        if s:
            cur, cnt = 0, 0
            okc = True
            for _ in range(n):
                j = s[cur] - 1
                if not (0 <= j < n): okc = False; break
                cnt += 1
                if j == 0: break
                cur = j
            good = okc and cnt == n
        large.append((n, bool(good)))
        if not good:
            print(f"LARGE n={n} FAIL: {out[:160]}")
    print(f"circuit_verify: OK={ok} LARGE_OK={large_ok} WRONG={wrong} "
          f"UNKNOWN={unknown} (N={N}, seed={seed}) large={large}")
    return 1 if wrong or any(not g for _, g in large) else 0


if __name__ == "__main__":
    sys.exit(main())
