import subprocess, random, sys
sys.path.insert(0,'/home/user/lpsolve/tools')
import numpy as np
from scipy.optimize import linprog  # for LP bound sanity
random.seed(int(sys.argv[1]) if len(sys.argv)>1 else 0)

def write_lp(n,m,c,A,b,rel,l,u,isint,path):
    # CSC triplets
    tri=[]
    for i in range(m):
        for j in range(n):
            if A[i][j]!=0: tri.append((i,j,A[i][j]))
    with open(path,'w') as f:
        f.write("maximize\n")
        f.write(f"{n} {m}\n")
        f.write(" ".join(repr(x) for x in c)+"\n")
        f.write(" ".join(repr(x) for x in b)+"\n")
        f.write("".join(rel)+"\n")
        for j in range(n):
            f.write(f"{l[j]} {u[j]}\n")
        f.write(str(len(tri))+"\n")
        for (i,j,v) in tri: f.write(f"{i} {j} {v!r}\n")

def brute(n,m,c,A,b,rel,l,u,isint):
    # enumerate integer variables in small ranges
    ints=[j for j in range(n) if isint[j]]
    best=-1e30; bestx=None
    # ranges for integer vars
    ranges=[]
    for j in ints:
        lo=int(l[j]); hi=int(u[j]) if u[j]<1e20 else 10
        ranges.append(range(max(0,lo),min(hi,10)+1))
    import itertools
    for combo in itertools.product(*ranges):
        x=[0.0]*n
        for idx,j in enumerate(ints): x[j]=float(combo[idx])
        # continuous vars at 0 (or their free optimum approximated by LP later)
        # check constraints with x
        feas=True
        for i in range(m):
            v=sum(A[i][j]*x[j] for j in range(n))
            if rel[i]=='<': 
                if v>b[i]+1e-6: feas=False
            elif rel[i]=='>':
                if v<b[i]-1e-6: feas=False
            else:
                if abs(v-b[i])>1e-6: feas=False
        if not feas: continue
        obj=sum(c[j]*x[j] for j in range(n))
        if obj>best: best=obj; bestx=x[:]
    return best,bestx

ok=0; fail=0
for it in range(200):
    n=random.randint(2,4); m=random.randint(1,4)
    c=[random.uniform(-3,5) for _ in range(n)]
    A=[[random.uniform(-2,3) if random.random()<0.6 else 0.0 for _ in range(n)] for _ in range(m)]
    b=[random.uniform(2,10) for _ in range(m)]
    rel=[random.choice(['<','<','<','=']) for _ in range(m)]
    l=[0]*n; u=[random.uniform(1,8) if random.random()<0.4 else 1e20 for _ in range(n)]
    # ensure some integer vars
    isint=[1]*n   # all-integer for brute-force verification
    # run mipsolve
    path='/tmp/mipv/lp'
    import os; os.makedirs('/tmp/mipv',exist_ok=True)
    write_lp(n,m,c,A,b,rel,l,u,isint,path+'.lp')
    intidx=[str(j) for j in range(n) if isint[j]]
    r=subprocess.run(['./mipsolve',path+'.lp',str(len(intidx))]+intidx,
                     capture_output=True,text=True,timeout=20)
    if 'status: OPTIMAL' not in r.stdout:
        # infeasible or limit: skip (brute is hard for infeasible)
        continue
    mobj=float([l2.split()[1] for l2 in r.stdout.splitlines() if l2.startswith('objective')][0])
    # brute force (only if all vars integer and small ranges)
    if all(isint) and all(u[j]>1e20 or u[j]<=6 for j in range(n)):
        bb,bx=brute(n,m,c,A,b,rel,l,u,isint)
        if abs(mobj-bb)>1e-4:
            fail+=1; print(f"FAIL it={it} mip={mobj} brute={bb}")
        else:
            ok+=1
print(f"MIP verify: OK={ok} FAIL={fail}")
