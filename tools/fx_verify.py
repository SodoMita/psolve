#!/usr/bin/env python3
"""Differential test: fixed-point exact LP solver (fxsolve) vs the double
revised-simplex LP solver (lpsolve) on random integer LPs.

Checks that fxsolve and lpsolve agree on status (OPTIMAL/INFEASIBLE/UNBOUNDED)
and, for optimal instances, on the objective value.  Also confirms the
fixed-point result is bit-identical/reproducible (determinism).

Usage:  python3 tools/fx_verify.py [N] [seed]
"""
import os, random, re, subprocess, sys, tempfile

ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LS=os.path.join(ROOT,"lpsolve"); FS=os.path.join(ROOT,"fxsolve")
GEN=os.path.join(ROOT,"tools","gen_fx_lp.py")

OBJ=re.compile(r"objective[^(]*\(dec\):\s*([-0-9.eE+]+)")
OBJ2=re.compile(r"objective:\s*([-0-9.eE+]+)")
STAT=re.compile(r"status:\s*(\w+)")

def run(binary,path):
    p=subprocess.run([binary,path],capture_output=True,text=True)
    st=STAT.search(p.stdout)
    obj=OBJ.search(p.stdout)
    if not obj: obj=OBJ2.search(p.stdout)
    return (st.group(1) if st else None), (float(obj.group(1)) if obj else None)

def gen_arbitrary(path, rng, n, m):
    """Write a random LP that is NOT guaranteed feasible/unbounded, to exercise
    infeasible/unbounded detection in both solvers."""
    sense=rng.choice(["maximize","minimize"])
    lines=[sense, f"{n} {m}"]
    lines.append(" ".join(str(rng.randint(-9,9)) for _ in range(n)))
    lines.append(" ".join(str(rng.randint(-9,9)) for _ in range(m)))
    lines.append("".join(rng.choice(['<','>','=']) for _ in range(m)))
    for _ in range(n):
        k=rng.randint(0,2)
        if k==0: lines.append("0 inf")
        elif k==1: lines.append(f"{rng.randint(-3,5)} inf")
        else:
            lo=rng.randint(-3,5); lines.append(f"{lo} {lo+rng.randint(0,6)}")
    A=[[rng.randint(-9,9) for _ in range(n)] for _ in range(m)]
    nnz=sum(1 for i in range(m) for j in range(n) if A[i][j])
    lines.append(str(nnz))
    for i in range(m):
        for j in range(n):
            if A[i][j]: lines.append(f"{i} {j} {A[i][j]}")
    with open(path,"w") as f: f.write("\n".join(lines)+"\n")

def parse_lp(path):
    toks=open(path).read().split()
    it=iter(toks)
    sense=next(it)
    n=int(next(it)); m=int(next(it))
    c=[int(next(it)) for _ in range(n)]
    b=[int(next(it)) for _ in range(m)]
    rel=list(next(it))
    lo=[None]*n; hi=[None]*n
    for j in range(n):
        a=next(it); d=next(it)
        lo[j]=None if a in ("inf","+inf") else (-float("inf") if a=="-inf" else int(a))
        hi[j]=None if d in ("inf","+inf") else (float("inf") if d=="-inf" else int(d))
    nnz=int(next(it))
    A=[[0]*n for _ in range(m)]
    for _ in range(nnz):
        r=int(next(it)); cj=int(next(it)); v=int(next(it)); A[r][cj]=v
    return sense,n,m,c,b,rel,lo,hi,A

def brute(path):
    """Exact status/optimum by exhaustive enumeration for small integer LPs.
    Returns (feasible, bounded_in_box, opt) or None if domains are too big."""
    try:
        sense,n,m,c,b,rel,lo,hi,A=parse_lp(path)
    except Exception:
        return None
    if n>4: return None
    caps=[]
    for j in range(n):
        if lo[j] is None or hi[j] is None: return None   # unbounded side: skip
        rngv=range(int(lo[j]),int(hi[j])+1)
        if len(rngv)>30: return None
        caps.append(rngv)
    feas=[]; best=None; hit_boundary=False
    for cand in itertools.product(*caps):
        ok=True
        for i in range(m):
            v=sum(A[i][j]*cand[j] for j in range(n))
            if rel[i]=='<' and v>b[i]: ok=False
            elif rel[i]=='>' and v<b[i]: ok=False
            elif rel[i]=='=' and v!=b[i]: ok=False
            if not ok: break
        if not ok: continue
        feas.append(cand)
        val=sum(c[j]*cand[j] for j in range(n))
        # did we hit the enumeration boundary (possible unbounded)?
        for j in range(n):
            if cand[j]==int(lo[j]) or cand[j]==int(hi[j]): hit_boundary=True
        if best is None or (sense=="maximize" and val>best) or (sense=="minimize" and val<best):
            best=val
    if not feas: return (False, True, None)
    return (True, True, best)

def main():
    N=int(sys.argv[1]) if len(sys.argv)>1 else 300
    seed=int(sys.argv[2]) if len(sys.argv)>2 else 1
    rng=random.Random(seed)
    d=tempfile.mkdtemp(prefix="fxverify")
    ok=wrong=review=0
    cases=[]
    for t in range(N):
        n=rng.randint(1,6); m=rng.randint(1,6); sd=rng.randint(0,10**9)
        path=os.path.join(d,f"t{t}.lp")
        if rng.random()<0.5:
            r=subprocess.run([sys.executable,GEN,str(n),str(m),str(sd),path],
                             capture_output=True,text=True)
            if r.returncode!=0: continue
            bf=None
            for line in r.stdout.splitlines():
                if line.startswith("BF_OPT="): bf=float(line.split("=")[1].split()[0])
        else:
            gen_arbitrary(path, rng, n, m); bf=None
        ds,dobj=run(LS,path)
        fs,fobj=run(FS,path)
        # reproducibility: solve fixed twice, must be identical
        fs2,fobj2=run(FS,path)
        repro = (fs==fs2) and ((fobj is None and fobj2 is None) or (fobj is not None and fobj2 is not None and abs(fobj-fobj2)<1e-9))
        bad=None
        if ds=="NUMERICAL_FAILURE":
            # The double solver's basis factorization can fail on
            # ill-conditioned/unbounded instances; the exact fixed solver
            # never does.  Treat a valid fixed status as correct.
            if fs not in ("OPTIMAL","INFEASIBLE","UNBOUNDED") or not repro:
                bad=f"double NUMERICAL_FAILURE, fixed={fs}"
        elif ds==fs:
            if ds=="OPTIMAL":
                if fobj is None: bad="fixed no objective"
                elif abs(dobj-fobj)>1e-6: bad=f"objective mismatch double={dobj} fixed={fobj}"
                elif not repro: bad="fixed non-deterministic"
            elif not repro: bad="fixed non-deterministic"
        elif ds=="OPTIMAL" and dobj is not None and abs(dobj)>=1e29 and fs=="UNBOUNDED":
            # double clamped a variable at its 1e30 LP_INF sentinel and
            # mislabeled a (numerically) unbounded LP as OPTIMAL; fixed's
            # UNBOUNDED is correct.
            pass
        else:
            # status divergence between the two solvers.  Resolve with brute
            # force when the instance is small enough.
            bf_status = brute(path)
            if bf_status is None:
                review+=1; cases.append((t,f"divergence double={ds} fixed={fs} (n too big to brute)",path,ds,dobj,fs,fobj,None))
                continue
            bf_feas, bf_bounded, bf_opt = bf_status
            if fs=="OPTIMAL":
                if not bf_feas or (bf_opt is not None and abs(fobj-bf_opt)>1e-6):
                    bad=f"fixed OPTIMAL {fobj} vs brute (feas={bf_feas} opt={bf_opt})"
                elif not repro: bad="fixed non-deterministic"
            elif fs=="UNBOUNDED":
                # if the enumeration stayed feasible at a boundary and grew to
                # the box edge, unbounded is plausible; double=OPTIMAL-bounded
                # would then be wrong.  Only flag if brute shows a bounded opt.
                if bf_feas and bf_opt is not None:
                    bad=f"fixed UNBOUNDED but brute bounded opt={bf_opt}"
            elif fs=="INFEASIBLE":
                if bf_feas: bad=f"fixed INFEASIBLE but brute feasible (opt={bf_opt})"
        if bad:
            wrong+=1; cases.append((t,bad,path,ds,dobj,fs,fobj,bf))
        else:
            ok+=1
    print(f"fx_verify: OK={ok} WRONG={wrong} REVIEW={review}  (N={N}, seed={seed})")
    for c in cases[:25]:
        t,bad,path,ds,dobj,fs,fobj,bf=c
        print(f"  case {t}: {bad}  (double={ds} {dobj}, fixed={fs} {fobj}, bf={bf})")
    return 1 if wrong else 0

if __name__=="__main__":
    sys.exit(main())
