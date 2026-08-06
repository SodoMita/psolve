#!/usr/bin/env python3
"""Benchmark my solver vs GLPK on bounded canonical LPs of varying size.
Reports wall time and objective agreement."""
import subprocess, os, time, sys
sys.path.insert(0, os.path.dirname(__file__))
os.makedirs('/tmp/benchlp', exist_ok=True)

def timeit(cmd):
    t0 = time.perf_counter()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    dt = time.perf_counter() - t0
    return r, dt

def run_case(n, m, dens, seed):
    base = f'/tmp/benchlp/p'
    subprocess.run(['python3','tools/gen_canonical.py',str(n),str(m),str(dens),str(seed),base],
                   capture_output=True, cwd='/home/user/lpsolve')
    # warmup / verify
    r, _ = timeit(['./lpsolve', base+'.lp'])
    myobj = None; myst = None; myiter=None
    for ln in r.stdout.splitlines():
        if ln.startswith('objective:'): myobj=float(ln.split()[1])
        if ln.startswith('status:'): myst=ln.split()[1]
        if ln.startswith('iterations:'): myiter=int(ln.split()[1])
    # best-of-3 timing for mine
    mine = min(timeit(['./lpsolve', base+'.lp'])[1] for _ in range(3))
    # glpk timing
    gr, gdt = timeit(['glpsol','-m',base+'.mod'])
    gdt = min(gdt, *[timeit(['glpsol','-m',base+'.mod'])[1] for _ in range(2)])
    gobj = None; gst = None
    for ln in gr.stdout.splitlines():
        if ln.startswith('OBJ'): gobj=float(ln.split()[1])
        if 'OPTIMAL LP SOLUTION FOUND' in ln: gst='OPTIMAL'
    ok = (myst=='OPTIMAL' and gst=='OPTIMAL' and gobj is not None and
          abs(myobj-gobj) <= 1e-5*max(1,abs(gobj)))
    return dict(n=n,m=m,dens=dens,mine=mine,glpk=gdt,ok=ok,myobj=myobj,gobj=gobj,iter=myiter)

def main():
    cases = [
        (100, 60, 0.2, 1), (200, 120, 0.15, 2), (300, 180, 0.1, 3),
        (500, 300, 0.08, 4), (800, 400, 0.06, 5),
    ]
    print(f"{'n':>5}{'m':>5}  {'mine(s)':>9}{'glpk(s)':>9}{'speedup':>8}  {'correct':>7}  obj")
    for (n,m,d,s) in cases:
        res = run_case(n,m,d,s)
        sp = res['glpk']/res['mine'] if res['mine']>0 else float('inf')
        print(f"{n:>5}{m:>5}  {res['mine']:9.4f}{res['glpk']:9.4f}{sp:8.2f}x  {str(res['ok']):>7}  {res['myobj'] and round(res['myobj'],1)}")

main()
