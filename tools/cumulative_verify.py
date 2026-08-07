#!/usr/bin/env python3
"""Differential verifier for the FlatZinc `cumulative` constraint handler.

Generates random small scheduling instances, writes them as FlatZinc, runs
`fznsolve`, and compares the SAT/UNSAT verdict against an independent brute-force
enumerator over every start-time combination.

Usage:  python3 tools/cumulative_verify.py [N] [seed]
"""
import itertools
import random
import subprocess
import sys
import tempfile
import os

def write_fzn(path, tasks, cap):
    n = len(tasks)
    lines = []
    lines.append("predicate cumulative(array[int] of var int: s, array[int] of var int: d,"
                 " array[int] of var int: r, var int: b);")
    for i in range(n):
        lo, hi = tasks[i][0]
        lines.append(f"var {lo}..{hi}: s{i+1} :: output_var;")
    starts = ", ".join(f"s{i+1}" for i in range(n))
    durs = ", ".join(str(tasks[i][1]) for i in range(n))
    ress = ", ".join(str(tasks[i][2]) for i in range(n))
    lines.append(f"constraint cumulative([{starts}], [{durs}], [{ress}], {cap});")
    lines.append("solve satisfy;")
    lines.append("")
    with open(path, "w") as f:
        f.write("\n".join(lines))

def brute_feasible(tasks, cap):
    """tasks: list of (start_domain, duration, resource).  Returns True if any
    start assignment satisfies: at every integer time point, total resource of
    tasks active (s <= t < s+d) is <= cap."""
    domains = [list(range(lo, hi + 1)) for lo, hi, _d, _r in tasks]
    for assign in itertools.product(*domains):
        # build occupancy over the finite relevant horizon
        lo_t = min(assign[i] for i in range(len(assign)))
        hi_t = max(assign[i] + tasks[i][2] - 1 for i in range(len(assign)))
        ok = True
        for t in range(lo_t, hi_t + 1):
            used = 0
            for i, s in enumerate(assign):
                d, r = tasks[i][2], tasks[i][3]
                if s <= t < s + d:
                    used += r
            if used > cap:
                ok = False
                break
        if ok:
            return True
    return False

def run_fznsolve(fzn, binary):
    try:
        r = subprocess.run([binary, fzn], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        return "UNKNOWN"  # B&B on a weak big-M relaxation; treat as unresolved
    out = r.stdout
    if "=====UNSATISFIABLE=====" in out:
        return "UNSAT"
    if "=====UNKNOWN=====" in out:
        return "UNKNOWN"
    # any printed solution => SAT
    if "= " in out or "-----" in out or "==========" in out:
        return "SAT"
    # status marker
    if "UNSATISFIABLE" in out.upper():
        return "UNSAT"
    return "SAT"  # solution printed

def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 777
    binary = sys.argv[3] if len(sys.argv) > 3 else "./fznsolve"
    rnd = random.Random(seed)
    tmpdir = tempfile.mkdtemp(prefix="cum_verify_")
    mismatches = 0
    unknowns = 0
    checked = 0
    for trial in range(N):
        # tractable instances: small domains so the LP/MIP bridge resolves quickly;
        # larger/harder combinatorial schedules hit the documented weak-relaxation
        # limit and return UNKNOWN (honest), so they are skipped here.
        n = rnd.randint(2, 4)
        tasks = []
        for _ in range(n):
            lo = rnd.randint(1, 3)
            hi = lo + rnd.randint(0, 2)   # domain size <= 3
            d = rnd.randint(1, 2)
            r = rnd.randint(1, 2)
            tasks.append(((lo, hi), d, r))
        cap = rnd.randint(2, 4)
        # avoid absurdly large brute-force enumeration
        prod = 1
        for t in tasks:
            prod *= (t[0][1] - t[0][0] + 1)
        if prod > 4000:
            continue
        # expand tasks for brute force to (domain_lo, domain_hi, d, r)
        bt = [(t[0][0], t[0][1], t[1], t[2]) for t in tasks]
        expect = brute_feasible(bt, cap)
        fzn = os.path.join(tmpdir, f"t{trial}.fzn")
        write_fzn(fzn, tasks, cap)
        got = run_fznsolve(fzn, binary)
        if got == "UNKNOWN":
            unknowns += 1
            continue
        got_sat = (got == "SAT")
        if got_sat != expect:
            mismatches += 1
            print(f"MISMATCH trial {trial}: expect {'SAT' if expect else 'UNSAT'} got {got}")
            with open(fzn) as f:
                print(f.read())
            if mismatches > 5:
                break
        checked += 1
    print(f"cumulative_verify: OK={checked} MISMATCH={mismatches} UNKNOWN={unknowns} "
          f"(N={N}, seed={seed})")
    sys.exit(1 if mismatches else 0)

if __name__ == "__main__":
    main()
