#!/usr/bin/env python3
import subprocess, os, time
os.makedirs('/tmp/benchs', exist_ok=True)
def timeit(cmd):
    t0=time.perf_counter()
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=900)
    return r,time.perf_counter()-t0
def one(n,m,dens,seed):
    base='/tmp/benchs/p'
    subprocess.run(['python3','tools/gen_canonical.py',str(n),str(m),str(dens),str(seed),base],capture_output=True,cwd='/home/user/lpsolve')
    r,_=timeit(['./lpsolve',base+'.lp'])
    obj=None;st=None;it=None
    for ln in r.stdout.splitlines():
        if ln.startswith('objective:'):obj=float(ln.split()[1])
        if ln.startswith('status:'):st=ln.split()[1]
        if ln.startswith('iterations:'):it=ln.split()[1]
    mine=min(timeit(['./lpsolve',base+'.lp'])[1] for _ in range(2))
    gr,g=min((timeit(['glpsol','-m',base+'.mod']) for _ in range(2)),key=lambda x:x[1])
    gobj=None
    for ln in gr.stdout.splitlines():
        if ln.startswith('OBJ'):gobj=float(ln.split()[1])
    ok = st=='OPTIMAL' and gobj is not None and abs(obj-gobj)<=1e-4*max(1,abs(gobj))
    return mine,g,ok
if __name__=='__main__':
    print(f"{'n':>6}{'m':>6}{'d':>5}  {'mine(s)':>9}{'glpk(s)':>9}{'speedup':>8}  {'correct':>7}")
    for (n,m,d) in [(500,300,0.05),(1000,600,0.02),(1500,900,0.012),(2000,1200,0.008),(3000,1800,0.005)]:
        mine,g,ok=one(n,m,d,9)
        sp=g/mine if mine>0 else float('inf')
        print(f"{n:>6}{m:>6}{d:>5}  {mine:9.3f}{g:9.3f}{sp:8.1f}x  {str(ok):>7}")
