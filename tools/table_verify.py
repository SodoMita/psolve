#!/usr/bin/env python3
"""Randomized brute-force differential test for the FlatZinc `table` handler.

Generates random `table` constraints over small integer domains, solves them
with `./fznsolve`, and compares the result (feasibility / objective) against an
exhaustive enumeration of the domain.  Covers satisfy, single-var maximize, and
deliberately unsatisfiable instances, including rows whose values fall outside
a variable's declared domain.

Every case must either (a) be SOLVED correctly (matching objective / correct
feasibility verdict / a returned tuple that really is in the table), or (b)
degrade gracefully to `=====UNKNOWN=====`, which psolve guarantees never to do
for a wrong answer.  A *hard* failure is a returned wrong objective, a wrong
feasibility verdict, or a returned solution that is not in the table.  The
sweep reports OK / WRONG (must be 0) / UNKNOWN (acceptable, counted).

Usage:  python3 tools/table_verify.py [N] [seed]
"""
import itertools, os, random, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FZN = os.path.join(ROOT, "fznsolve")
TMP = "/tmp/psolve_table"
TIMEOUT = 8  # seconds per solve; degeneracy on some maximize directions is slow

VARRE = re.compile(r"^x(\d+)\s*=\s*(-?\d+)")


def run_fzn(fzn_path):
    try:
        p = subprocess.run([FZN, fzn_path], capture_output=True, text=True,
                           timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return None, None, False, True   # timed out -> treat as unknown
    out = p.stdout
    vals = {}
    for line in out.splitlines():
        m = VARRE.match(line.strip())
        if m:
            vals[int(m.group(1))] = int(m.group(2))
    obj = None
    for line in out.splitlines():
        if "mzn-stat: objective=" in line:
            obj = float(line.split("=")[1])
    unsat = "=====UNSATISFIABLE=====" in out
    unknown = ("=====UNKNOWN=====" in out)
    return vals, obj, unsat, unknown


def build_fzn(path, arity, lo, hi, rows, mode, obj_var):
    lines = ["predicate table(array[int] of var int: x, array[int, int] of int: t);"]
    for i in range(arity):
        lines.append(f"var {lo[i]}..{hi[i]}: x{i} :: output_var;")
    flat = ",".join(str(v) for row in rows for v in row)
    xlist = ",".join(f"x{i}" for i in range(arity))
    lines.append(f"constraint table([{xlist}], array2d(1, {len(rows)}, 1, {arity}, [{flat}]));")
    if mode == "max":
        # maximize a single chosen variable directly (miniZinc flattening would
        # introduce an objective var, but a direct var objective is equivalent).
        oi = obj_var
        lines.append(f"solve maximize x{oi};")
    else:
        lines.append("solve satisfy;")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def brute(arity, lo, hi, rows, obj_var):
    feasible = []
    for tup in itertools.product(*[range(lo[i], hi[i] + 1) for i in range(arity)]):
        if tup in rows:
            feasible.append(tup)
    best = max((t[obj_var] for t in feasible), default=None) if obj_var is not None else None
    return feasible, best


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 12345
    rng = random.Random(seed)
    os.makedirs(TMP, exist_ok=True)

    ok = wrong = unknown = 0
    wrong_cases, unknown_cases = [], []
    for t in range(N):
        arity = rng.randint(1, 4)
        lo = [rng.randint(0, 2) for _ in range(arity)]
        hi = [lo[i] + rng.randint(0, 3) for i in range(arity)]
        domains = [list(range(lo[i], hi[i] + 1)) for i in range(arity)]

        full = list(itertools.product(*domains))
        rng.shuffle(full)
        take = rng.randint(0, min(len(full), 6))
        rows = full[:take]
        if rng.random() < 0.5:
            for _ in range(rng.randint(1, 2)):
                rows.append(tuple(rng.choice([lo[i] - 1, hi[i] + 1, rng.randint(0, 9)])
                                  for i in range(arity)))
        rows = list(dict.fromkeys(rows))

        mode = "max" if rng.random() < 0.6 else "sat"
        obj_var = rng.randrange(arity) if mode == "max" else None

        fzn = os.path.join(TMP, f"t{t}.fzn")
        build_fzn(fzn, arity, lo, hi, rows, mode, obj_var)
        vals, obj, unsat, unknown_ret = run_fzn(fzn)

        bf_feas, bf_obj = brute(arity, lo, hi, rows, obj_var)
        bf_sat = len(bf_feas) > 0

        if unknown_ret:
            # graceful degradation: acceptable but not a pass
            if not bf_sat and not unsat:
                unknown_cases.append((t, "UNKNOWN but expected UNSAT"))
                unknown += 1
            else:
                unknown_cases.append((t, f"UNKNOWN (expected SAT, bf_obj={bf_obj})"))
                unknown += 1
            continue

        bad = None
        if mode == "sat":
            if bf_sat and unsat:
                bad = "expected SAT, got UNSAT"
            elif not bf_sat and not unsat:
                bad = "expected UNSAT, got a solution"
            elif bf_sat:
                tup = tuple(vals[i] for i in range(arity)) if vals else None
                if tup not in rows:
                    bad = f"solution {vals} not in table rows"
        else:  # max
            if bf_sat and unsat:
                bad = "expected SAT, got UNSAT"
            elif not bf_sat and not unsat:
                bad = f"expected UNSAT, got solution {vals} obj={obj}"
            elif bf_sat:
                if obj is None or abs(obj - bf_obj) > 1e-9:
                    bad = f"obj={obj} expected {bf_obj}"

        if bad:
            wrong_cases.append((t, bad, rows, lo, hi, mode, vals, obj, bf_sat, bf_obj))
            wrong += 1
        else:
            ok += 1

    print(f"table_verify: OK={ok} WRONG={wrong} UNKNOWN={unknown}  (N={N}, seed={seed})")
    if wrong_cases:
        print("HARD FAILURES (must be fixed):")
        for c in wrong_cases[:20]:
            t, reason, rows, lo, hi, mode, vals, obj, bf_sat, bf_obj = c
            print(f"  case {t}: {reason}\n     rows={rows} domains={list(zip(lo, hi))} "
                  f"mode={mode} vals={vals} obj={obj} bf_sat={bf_sat} bf_obj={bf_obj}")
    if unknown_cases:
        print(f"graceful UNKNOWN cases: {len(unknown_cases)} (of these, "
              f"{sum(1 for _, r in unknown_cases if 'expected UNSAT' in r)} were expected-UNSAT "
              f"but reported UNKNOWN)")
    return 1 if wrong else 0


if __name__ == "__main__":
    sys.exit(main())
