#!/usr/bin/env python3
"""Generate random LPs and emit both the solver's .lp format and a GLPK .mod.

Canonical structure:  maximize  c^T x
                      s.t.      A x (<= or =) b,  x in [l,u]
Nonnegativity is a special case of the general bounds.

Usage:  gen.py <n> <m> <density> <seed> <outbase>
"""
import sys, random

def gen(n, m, density, seed, outbase):
    rng = random.Random(seed)
    maximize = True
    # bounds: x in [0, inf] by default; occasionally boxed with finite upper
    lo = [0.0]*n
    hi = [float('inf')]*n
    for j in range(n):
        if rng.random() < 0.30:
            hi[j] = rng.uniform(1, 50)      # boxed
        if rng.random() < 0.10:
            lo[j] = rng.uniform(-20, -1)    # negative lower bound

    c = [rng.uniform(-10, 10) for _ in range(n)]

    # sparse A with positive rhs so <= rows have slack-basis feasibility
    A = [[0.0]*n for _ in range(m)]
    for i in range(m):
        for j in range(n):
            if rng.random() < density:
                A[i][j] = rng.uniform(-5, 5)
    rel = ['<']*m
    # some equality rows (these force Phase I real pivots)
    for i in range(m):
        if rng.random() < 0.25:
            rel[i] = '='
    b = [rng.uniform(1, 30) for _ in range(m)]

    # ---- write solver .lp ----
    with open(outbase + '.lp', 'w') as f:
        f.write("maximize\n")
        f.write(f"{n} {m}\n")
        f.write(" ".join(repr(x) for x in c) + "\n")
        f.write(" ".join(repr(x) for x in b) + "\n")
        f.write("".join(rel) + "\n")
        for j in range(n):
            lo_s = "inf" if lo[j] <= -1e20 else repr(lo[j])
            hi_s = "inf" if hi[j] >= 1e20 else repr(hi[j])
            f.write(f"{lo_s} {hi_s}\n")
        tri = []
        for i in range(m):
            for j in range(n):
                if A[i][j] != 0.0:
                    tri.append((i, j, A[i][j]))
        f.write(str(len(tri)) + "\n")
        for (i, j, v) in tri:
            f.write(f"{i} {j} {v!r}\n")

    # ---- write GLPK .mod ----
    with open(outbase + '.mod', 'w') as f:
        f.write(f"param n := {n};\nparam m := {m};\n")
        f.write("set V := 1..n;\n")
        f.write("param c{V};\nparam b{1..m};\n")
        f.write("param A{1..m, V};\n")
        for j in range(n):
            loj = "" if lo[j] <= -1e20 else f" >= {lo[j]:.17g}"
            hij = "" if hi[j] >= 1e20 else f" <= {hi[j]:.17g}"
            f.write(f"var x{j+1}{loj}{hij};\n")
        f.write("maximize obj: " + " + ".join(f"{c[j]:.17g}*x{j+1}" for j in range(n)) + ";\n")
        for i in range(m):
            lhs = " + ".join(f"{A[i][j]:.17g}*x{j+1}" for j in range(n) if A[i][j]!=0)
            if not lhs: lhs = "0"
            op = "=" if rel[i]=='=' else "<="
            f.write(f"s.t. r{i+1}: {lhs} {op} {b[i]:.17g};\n")
        f.write("solve;\nprintf \"OBJ %.15g\\n\", obj;\nend;\n")

def main():
    n, m, density, seed, outbase = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    gen(n, m, density, seed, outbase)

if __name__ == "__main__":
    main()
