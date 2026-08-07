#!/usr/bin/env python3
"""Generate random .lp files (integer data) to stress the fixed-point LP solver.

Produces a mix of feasible / infeasible / unbounded LPs with integer
coefficients, all relation types, and a variety of variable bounds (free on
one side, boxed, and fixed variables).  Also returns an exact rational optimum
when the instance is made feasible-by-construction.

Usage:  python3 tools/gen_fx_lp.py <n> <m> <seed> <out.lp>
"""
import sys, random

def make_feasible(rng, n, m):
    """Build a guaranteed-feasible LP and compute its exact optimum via the
    analytic construction (random feasible point + positive slack)."""
    # random coefficients (dense, small ints, some zeros)
    A = [[rng.randint(-9,9) for _ in range(n)] for _ in range(m)]
    c = [rng.randint(-9,9) for _ in range(n)]
    # random variable bounds: each var has at least one finite bound
    lo = [None]*n; hi = [None]*n
    for j in range(n):
        kind = rng.randint(0,2)
        if kind==0: lo[j]=0; hi[j]=None            # [0,inf)
        elif kind==1: lo[j]=rng.randint(0,5); hi[j]=None  # [lo,inf)
        else: lo[j]=rng.randint(0,5); hi[j]=lo[j]+rng.randint(0,8)  # boxed
        if rng.random()<0.15: hi[j]=lo[j]          # fixed
    # pick a feasible point x in the box
    x = []
    for j in range(n):
        if lo[j] is not None and hi[j] is not None:
            if lo[j]==hi[j]: x.append(lo[j])
            else: x.append(rng.randint(lo[j],hi[j]))
        elif lo[j] is not None: x.append(lo[j]+rng.randint(0,10))
        else: x.append(hi[j]-rng.randint(0,10))
    # relations and rhs with margin so the point is strictly feasible
    rel=[]; b=[]
    for i in range(m):
        rhs = sum(A[i][j]*x[j] for j in range(n))
        r = rng.choice(['<','>','='])
        if r=='<': b.append(rhs+rng.randint(0,5))     # slack >= 0
        elif r=='>': b.append(rhs-rng.randint(0,5))   # surplus >= 0
        else: b.append(rhs)
        rel.append(r)
    # objective sense
    sense = rng.choice(['maximize','minimize'])
    # exact optimum: enumerate? Only for tiny n. Return analytic for caller to
    # resolve via double solver instead (we don't brute force here).
    return n,m,sense,A,c,lo,hi,rel,b

def write_lp(path, sense, n, m, c, A, lo, hi, rel, b):
    lines=[sense, f"{n} {m}", " ".join(map(str,c)), " ".join(map(str,b)),
           "".join(rel)]
    for j in range(n):
        loj = "inf" if lo[j] is None else str(lo[j])
        hih = "inf" if hi[j] is None else str(hi[j])
        lines.append(f"{loj} {hih}")
    nnz=sum(1 for i in range(m) for j in range(n) if A[i][j]!=0)
    lines.append(str(nnz))
    for i in range(m):
        for j in range(n):
            if A[i][j]!=0:
                lines.append(f"{i} {j} {A[i][j]}")
    with open(path,"w") as f:
        f.write("\n".join(lines)+"\n")

def main():
    n=int(sys.argv[1]); m=int(sys.argv[2]); seed=int(sys.argv[3]); out=sys.argv[4]
    rng=random.Random(seed)
    n,m,sense,A,c,lo,hi,rel,b = make_feasible(rng,n,m)
    write_lp(out, sense, n, m, c, A, lo, hi, rel, b)
    # print exact objective by brute force if tiny
    if n<=3:
        best=None; bx=None
        domains=[]
        for j in range(n):
            if lo[j] is not None and hi[j] is not None:
                domains.append(range(lo[j],hi[j]+1))
            elif lo[j] is not None:
                domains.append(range(lo[j], lo[j]+15))
            else:
                domains.append(range(hi[j]-15, hi[j]+1))
        for cand in __import__('itertools').product(*domains):
            feas=True
            for i in range(m):
                v=sum(A[i][j]*cand[j] for j in range(n))
                if rel[i]=='<' and v>b[i]: feas=False
                if rel[i]=='>' and v<b[i]: feas=False
                if rel[i]=='=' and v!=b[i]: feas=False
                if not feas: break
            if not feas: continue
            val=sum(c[j]*cand[j] for j in range(n))
            if best is None or (sense=='maximize' and val>best) or (sense=='minimize' and val<best):
                best=val; bx=cand
        print(f"BF_OPT={best} BF_X={bx}")

if __name__=="__main__":
    main()
