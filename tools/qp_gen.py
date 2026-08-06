#!/usr/bin/env python3
"""Generate random convex QPs, solve with the C solver (via a small CLI) and
verify against scipy.optimize.minimize (SLSQP).  Emits the QP in a simple text
format and reads back the C solution."""
import sys, os, subprocess, random
os.makedirs('/tmp/qpgen', exist_ok=True)
import numpy as np

def write_qp(path, n, m, Q, c, A, b):
    # Q, A are numpy arrays (row-major storage).  The C reader wants Q and A
    # column-major: for each column j, the column entries i=0..n-1.
    with open(path, 'w') as f:
        f.write(f"{n} {m}\n")
        f.write(" ".join(repr(float(x)) for x in c) + "\n")
        for j in range(n):
            f.write(" ".join(repr(float(Q[i, j])) for i in range(n)) + "\n")  # column j
        for i in range(m):
            f.write(" ".join(repr(float(A[i, j])) for j in range(n)) + "\n")
        f.write(" ".join(repr(float(x)) for x in b) + "\n")

def main():
    seed = int(sys.argv[1])
    rng = random.Random(seed)
    n = 2 + rng.randint(0, 5)
    m = 1 + rng.randint(0, n + 2)
    # PD Q = G^T G + eps I
    G = np.array([[rng.uniform(-2,2) for _ in range(n)] for _ in range(n)])
    Q = G.T @ G + 0.1*np.eye(n)
    c = np.array([rng.uniform(-5,5) for _ in range(n)])
    A = np.array([[rng.uniform(-3,3) for _ in range(n)] for _ in range(m)])
    # ensure origin feasible (b >= 0) but also test a few infeasible-origin via negative b occasionally
    if seed % 5 == 0:
        b = np.array([rng.uniform(-5, 5) for _ in range(m)])  # some negative -> Phase-I needed
    else:
        b = np.array([rng.uniform(0, 5) for _ in range(m)])
    base = "/tmp/qpgen/p"
    write_qp(base + '.qp', n, m, Q, c, A, b)
    r = subprocess.run(['/tmp/qpsolve', base + '.qp'], capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        return dict(ok=False, reason='crash '+r.stderr[-100:])
    xC = None; statusC = 0
    for ln in r.stdout.splitlines():
        if ln.startswith('SOLUTION'):
            xC = np.array([float(v) for v in ln.split()[1:]])
        if ln.startswith('STATUS'):
            statusC = int(ln.split()[1])
    # infeasible region: C should report -1 and scipy should agree
    from scipy.optimize import linprog
    lpr = linprog(np.zeros(n), A_ub=A, b_ub=b, bounds=[(None,None)]*n, method='highs')
    if statusC != 0:
        if lpr.status != 2:   # C says infeasible but problem is feasible -> fail
            return dict(ok=False, reason='C said infeasible but problem feasible')
        return dict(ok=True, infeasible=True, n=n, m=m)
    if xC is None:
        return dict(ok=False, reason='no solution line')
    # scipy reference (SLSQP handles inequality)
    from scipy.optimize import minimize
    def obj(x): return 0.5*x@Q@x + c@x
    cons = [{'type':'ineq','fun':lambda x,i=i: b[i]-A[i]@x} for i in range(m)]
    # multi-start to avoid local issues (convex so should be fine)
    res = minimize(obj, np.zeros(n), constraints=cons, method='SLSQP')
    if not res.success:
        return dict(ok=False, reason='scipy failed')
    diff = np.linalg.norm(xC - res.x)
    objC = obj(xC); objS = obj(res.x)
    return dict(ok=True, diff=diff, objC=objC, objS=objS, n=n, m=m,
                feasC=all(b[i]-A[i]@xC >= -1e-6 for i in range(m)))

if __name__ == '__main__':
    res = main()
    if res.get('ok') and res.get('infeasible'):
        print("OK infeasible n=%d m=%d" % (res['n'], res['m']))
    elif res.get('ok'):
        print("OK n=%d m=%d |x-mine-scipy|=%g obj_mine=%.6g obj_scipy=%.6g feas=%s" %
              (res['n'], res['m'], res['diff'], res['objC'], res['objS'], res['feasC']))
    else:
        print("FAIL", res.get('reason'), res)
