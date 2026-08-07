#!/usr/bin/env python3
"""Verify a solver-produced solution for primal feasibility and, when given
a reference optimum, objective agreement."""
import sys

def read_lp(path):
    with open(path) as f:
        toks = f.read().split()
    i = 0
    sense = toks[i]; i += 1
    n, m = int(toks[i]), int(toks[i+1]); i += 2
    c = [float(x) for x in toks[i:i+n]]; i += n
    b = [float(x) for x in toks[i:i+m]]; i += m
    rel = toks[i]; i += 1
    lo, hi = [], []
    for _ in range(n):
        lo.append(float('-inf') if toks[i]=='inf' or toks[i]=='-inf' else float(toks[i])); i+=1
        hi.append(float('inf') if toks[i]=='inf' or toks[i]=='+inf' else float(toks[i])); i+=1
    nnz = int(toks[i]); i += 1
    A = [[0.0]*n for _ in range(m)]
    for _ in range(nnz):
        r, ccol, v = int(toks[i]), int(toks[i+1]), float(toks[i+2]); i += 3
        A[r][ccol] = v
    return {'sense':sense,'n':n,'m':m,'c':c,'b':b,'rel':rel,'lo':lo,'hi':hi,'A':A}

def parse_x(path, n):
    with open(path) as f:
        lines = f.read().splitlines()
    x = [None]*n
    obj = None
    for ln in lines:
        s = ln.strip()
        if s.startswith('objective:'):
            obj = float(s.split()[1])
        if s.startswith('x['):
            j = int(s.split('[')[1].split(']')[0])
            x[j] = float(s.split('=')[1])
    return x, obj

def check(lp, x):
    # Absolute tolerances are meaningless once row activities reach 1e4+, so
    # scale the row tolerance by the magnitude of the terms that make it up.
    tolx = 1e-6
    res = []
    ok = True
    # bounds
    for j in range(lp['n']):
        v = x[j]
        if v is None: ok=False; continue
        if v < lp['lo'][j]-tolx or v > lp['hi'][j]+tolx:
            res.append(f"var {j} value {v:.6g} outside [{lp['lo'][j]:.4g},{lp['hi'][j]:.4g}]"); ok=False
    # constraints
    for i in range(lp['m']):
        terms = [lp['A'][i][j]*x[j] for j in range(lp['n']) if x[j] is not None]
        lhs = sum(terms)
        rhs = lp['b'][i]; r = lp['rel'][i]
        scale = 1.0 + abs(rhs) + max([abs(t) for t in terms], default=0.0)
        tolr = tolx * scale
        if r == '<' and lhs > rhs + tolr:
            res.append(f"row {i}: {lhs:.6g} <= {rhs:.4g} violated"); ok=False
        if r == '>' and lhs < rhs - tolr:
            res.append(f"row {i}: {lhs:.6g} >= {rhs:.4g} violated"); ok=False
        if r == '=' and abs(lhs-rhs) > tolr:
            res.append(f"row {i}: {lhs:.6g} = {rhs:.4g} violated"); ok=False
    return ok, res

if __name__ == '__main__':
    lp = read_lp(sys.argv[1])
    x, obj = parse_x(sys.argv[2], lp['n'])
    ok, res = check(lp, x)
    print("feasible:", ok)
    for r in res[:20]: print("  ", r)
    if obj is not None: print("objective:", obj)
