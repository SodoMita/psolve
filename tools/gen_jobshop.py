#!/usr/bin/env python3
"""Standard jobshop FlatZinc generator (the shape CP-SAT and fzn-cp-sat emit).

Each job is a chain of operations; each machine is a `disjunctive` constraint
over the operations that use it (so every machine serialises its operations),
and the objective is the makespan:

    var 0..H: s_{job,op} :: output_var;
    var 0..H: mk :: output_var;
    constraint int_le(s + d, mk);                     % makespan rows
    constraint int_le(s_{j,k-1} + d_{j,k-1}, s_{j,k}); % precedence inside a job
    constraint disjunctive([...], [d,...]);           % one per machine
    solve minimize mk;

Usage: gen_jobshop.py <jobs> <machines> [seed] [--out FILE]
"""
from __future__ import annotations

import random
import sys


def gen(jobs: int, machines: int, seed: int):
    rng = random.Random(seed)
    # random machine order per job, random durations 1..9
    route = []
    for j in range(jobs):
        ops = [(m, rng.randint(1, 9)) for m in rng.sample(range(machines), machines)]
        route.append(ops)
    total = sum(d for ops in route for _, d in ops)
    horizon = total
    lines = []
    names = {}
    for j in range(jobs):
        for k in range(machines):
            names[(j, k)] = f"s{j}_{k}"
            lines.append(f"var 0..{horizon}: s{j}_{k} :: output_var;")
    lines.append(f"var 0..{horizon}: mk :: output_var;")
    for j in range(jobs):
        for k, (m, d) in enumerate(route[j]):
            lines.append(f"constraint int_le({names[(j, k)]}+{d}, mk);")
            if k:
                lines.append(f"constraint int_le({names[(j, k - 1)]}+{route[j][k - 1][1]}, "
                             f"{names[(j, k)]});")
    for m in range(machines):
        vs, ds = [], []
        for j in range(jobs):
            for k, (mm, d) in enumerate(route[j]):
                if mm == m:
                    vs.append(names[(j, k)])
                    ds.append(d)
        lines.append(f"constraint disjunctive([{','.join(vs)}],[{','.join(map(str, ds))}]);")
    lines.append("solve minimize mk;")
    return "\n".join(lines) + "\n", route


def main():
    jobs = int(sys.argv[1]) if len(sys.argv) > 1 else 7
    machines = int(sys.argv[2]) if len(sys.argv) > 2 else 7
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    text, route = gen(jobs, machines, seed)
    if "--out" in sys.argv:
        path = sys.argv[sys.argv.index("--out") + 1]
        with open(path, "w") as fh:
            fh.write(text)
        print(f"wrote {path} ({jobs}x{machines}, seed {seed})")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
