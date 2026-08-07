#!/usr/bin/env python3
"""Randomized FlatZinc->MIP verification: generate small integer LP constraints,
solve with fznsolve, check integrality + optimality against brute force."""
import subprocess, random, sys, itertools, os

def make_fzn(n, m, c, A, b, rel, path):
    lines=["predicate int_lin_le(array[int] of int: c, array[int] of var int: x, int: d);",
           "predicate int_lin_eq(array[int] of int: c, array[int] of var int: x, int: d);"]
    for j in range(n):
        lines.append(f"var 0..20: x{j} :: output_var;")
    for i in range(m):
        coefs=", ".join(str(A[i][j]) for j in range(n))
        varx=", ".join(f"x{j}" for j in range(n))
        pred = "int_lin_eq" if rel[i]=='=' else "int_lin_le"
        lines.append(f"constraint {pred}([{coefs}], [{varx}], {b[i]});")
    lines.append("solve maximize x0;")
    open(path,'w').write("\n".join(lines))

def brute(n,c,A,b,rel):
    best=-1e9; bestx=None
    for combo in itertools.product(range(21), repeat=n):
        ok=True
        for i in range(len(b)):
            v=sum(A[i][j]*combo[j] for j in range(n))
            if rel[i]=='<':
                if v>b[i]: ok=False
            else:
                if v!=b[i]: ok=False
        if not ok: continue
        if combo[0]>best: best=combo[0]; bestx=combo
    return best,bestx

def main():
    seed=int(sys.argv[1]) if len(sys.argv)>1 else 0
    rng=random.Random(seed)
    ok=fail=0
    for t in range(60):
        n=rng.randint(2,4); m=rng.randint(1,3)
        c=[0]*n
        A=[[rng.randint(-2,4) if rng.random()<0.7 else 0 for _ in range(n)] for _ in range(m)]
        b=[rng.randint(5,20) for _ in range(m)]
        rel=[rng.choice(['<','<','<','=']) for _ in range(m)]
        path='/tmp/fzmv/t.fzn'; os.makedirs('/tmp/fzmv',exist_ok=True)
        make_fzn(n,m,c,A,b,rel,path)
        r=subprocess.run(['./fznsolve',path],capture_output=True,text=True,timeout=20)
        # parse solution
        vals={}
        for ln in r.stdout.splitlines():
            ln=ln.strip()
            if '=' in ln and ';' in ln and 'mzn' not in ln:
                try:
                    name,val=ln.split('=',1); vals[name.strip()]=int(val.strip().rstrip(';'))
                except: pass
        bb,bx=brute(n,c,A,b,rel)
        if 'UNSAT' in r.stdout:
            if bb>=0:  # solver says infeasible but brute finds a solution
                fail+=1; print(f"FAIL t={t} solver UNSAT but brute obj={bb}")
            continue
        if 'UNKNOWN' in r.stdout:
            fail+=1; print(f"FAIL t={t} solver UNKNOWN but brute obj={bb}"); continue
        if bb<0:
            # solver found a solution but brute says infeasible -> solver wrong
            fail+=1; print(f"FAIL t={t} solver found sol but brute infeasible"); continue
        # check integrality + optimality
        x0=vals.get('x0')
        if x0 is None:
            fail+=1; print(f"FAIL t={t} no x0"); continue
        if x0!=bb:
            fail+=1; print(f"FAIL t={t} x0={x0} brute={bb} brute_x={bx}"); continue
        ok+=1
    print(f"FZ->MIP verify: OK={ok} FAIL={fail}")

if __name__=='__main__':
    main()
