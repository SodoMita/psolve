#!/usr/bin/env python3
"""Differential test: LP solver vs scipy on equality-constrained / fixed-variable
problems (exercises the Phase I artificial-drive-out path)."""
import subprocess, random, sys
from scipy.optimize import linprog

def make_rel(m):
    # generate relations such that the concatenated token is unambiguous:
    # no '<'/'>' immediately followed by '=' (which would merge into <= / >=).
    while True:
        rel = [random.choice('=<><=') for _ in range(m)]
        ok = True
        for i in range(m-1):
            if rel[i] in '<>' and rel[i+1] == '=':
                ok = False; break
        if ok:
            return rel

def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    random.seed(seed)
    ok = fail = 0
    for t in range(300):
        n = random.randint(2, 5); m = random.randint(1, 3)
        sign = random.choice([1, -1])
        A = [[random.randint(-3,3) if random.random()<0.7 else 0 for _ in range(n)] for _ in range(m)]
        rel = make_rel(m)
        b = [random.randint(0,15) for _ in range(m)]
        fixed = {}
        if random.random() < 0.5:
            j = random.randint(1, n-1); fixed[j] = random.randint(0, 12)
        lo = [0]*n; hi = [20]*n
        for j, v in fixed.items(): lo[j] = hi[j] = v
        tri = [(i,j,A[i][j]) for i in range(m) for j in range(n) if A[i][j]!=0]
        with open('/tmp/eqf.lp','w') as f:
            f.write("maximize\n%d %d\n" % (n, m))
            f.write(" ".join(str(sign if j==0 else 0) for j in range(n)) + "\n")
            f.write(" ".join(str(b[i]) for i in range(m)) + "\n")
            f.write("".join(rel) + "\n")
            for j in range(n): f.write("%d %d\n" % (lo[j], hi[j]))
            f.write(str(len(tri)) + "\n")
            for (i,j,v) in tri: f.write("%d %d %d\n" % (i, j, v))
        try:
            r = subprocess.run(['./lpsolve','/tmp/eqf.lp'], capture_output=True, text=True, timeout=10)
        except subprocess.TimeoutExpired:
            fail += 1; print(f"t={t} TIMEOUT"); continue
        myst = [l.split()[1] for l in r.stdout.splitlines() if l.startswith('status:')]
        myobj = [float(l.split()[1]) for l in r.stdout.splitlines() if l.startswith('objective:')]
        if not myst:
            fail += 1
            if fail<8: print(f"t={t} no status rc={r.returncode} err={r.stderr.strip()[:60]}")
            continue
        cc = [-sign if j==0 else 0 for j in range(n)]
        Aub=[];bub=[];Aeq=[];beq=[]
        for i in range(m):
            row=[A[i][j] for j in range(n)]
            if rel[i]=='=': Aeq.append(row); beq.append(b[i])
            elif rel[i]=='<': Aub.append(row); bub.append(b[i])
            else:  # '>'
                Aub.append([-x for x in row]); bub.append(-b[i])
        sc = linprog(cc, A_ub=Aub or None, b_ub=bub or None, A_eq=Aeq or None,
                     b_eq=beq or None, bounds=list(zip(lo,hi)), method='highs')
        if sc.status == 0:
            good = myst==['OPTIMAL'] and abs(myobj[0]-(-sc.fun)) < 1e-5*max(1,abs(sc.fun))
            if good: ok += 1
            else:
                fail += 1
                if fail<8: print(f"t={t} mine={myst} obj={myobj} scipy={-sc.fun}")
        elif myst==['INFEASIBLE'] and sc.status==2:
            ok += 1
        else:
            fail += 1
            if fail<8: print(f"t={t} mine={myst} scipy_st={sc.status}")
    print(f"seed {seed} eq/fixed vs scipy: OK={ok} FAIL={fail}")

main()
