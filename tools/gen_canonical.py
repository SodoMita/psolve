#!/usr/bin/env python3
"""Guaranteed feasible & bounded canonical LPs:
   maximize c^T x  s.t.  A x <= b,  x >= 0,  sum x <= C
   A >= 0 entries, b > 0, plus a cap row so the polytope is bounded.
   Emits .lp and .mod."""
import sys, random
sys.path.insert(0, __import__('os').path.dirname(__file__))
from gen import gen

def main():
    n,m,dens,seed,outbase = int(sys.argv[1]),int(sys.argv[2]),float(sys.argv[3]),int(sys.argv[4]),sys.argv[5]
    rng = random.Random(seed)
    c = [rng.uniform(-10,10) for _ in range(n)]
    A = [[0.0]*n for _ in range(m)]
    for i in range(m):
        for j in range(n):
            if rng.random() < dens: A[i][j] = rng.uniform(0.1, 5)
    rel = ['<']*(m+1)
    b = [rng.uniform(5, 30) for _ in range(m)]
    # cap row: sum x_j <= C  -> bounded
    C = rng.uniform(20, 60)
    A.append([1.0]*n); b.append(C)
    mm = m+1
    # write .lp (reuse a writer here)
    with open(outbase+'.lp','w') as f:
        f.write("maximize\n"); f.write(f"{n} {mm}\n")
        f.write(" ".join(repr(x) for x in c)+"\n")
        f.write(" ".join(repr(x) for x in b)+"\n")
        f.write("".join(rel)+"\n")
        for _ in range(n): f.write("0 inf\n")
        tri=[]
        for i in range(mm):
            for j in range(n):
                if A[i][j]!=0: tri.append((i,j,A[i][j]))
        f.write(str(len(tri))+"\n")
        for (i,j,v) in tri: f.write(f"{i} {j} {v!r}\n")
    # .mod
    with open(outbase+'.mod','w') as f:
        for j in range(n):
            loj = "" if c[j] is None else ""
            f.write(f"var x{j+1} >= 0;\n")
        f.write("maximize obj: "+" + ".join(f"{c[j]:.17g}*x{j+1}" for j in range(n))+";\n")
        for i in range(mm):
            lhs = " + ".join(f"{A[i][j]:.17g}*x{j+1}" for j in range(n) if A[i][j]!=0) or "0"
            f.write(f"s.t. r{i+1}: {lhs} <= {b[i]:.17g};\n")
        f.write("solve;\nprintf \"OBJ %.15g\\n\", obj;\nend;\n")

if __name__=="__main__": main()
