#!/usr/bin/env python3
"""Differential tester: my solver vs GLPK, plus primal-feasibility check."""
import sys, subprocess, os, random
sys.path.insert(0, os.path.dirname(__file__))
from verify import read_lp, parse_x, check
import gen

def run(cmd, **kw):
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=60, **kw)
    return p.returncode, p.stdout, p.stderr

def glpk_status(out):
    o = out.lower()
    # glpsol uses several phrasings across versions; recognize them all.
    # Check infeasible/unbounded FIRST so a stray "optimal" in a message or
    # header cannot override a clear status verdict.
    if ('no primal feasible solution' in o
        or 'problem has no primal feasible solution' in o): return 'INFEASIBLE'
    if 'unbounded primal solution' in o: return 'UNBOUNDED'
    # "LP HAS NO DUAL FEASIBLE SOLUTION" is a *dual* infeasibility verdict from
    # the presolver: the primal is then unbounded OR infeasible, and glpsol
    # does not distinguish.  Treat it as either (verified against HiGHS: e.g.
    # difftest seed 156 is genuinely INFEASIBLE while glpsol prints this).
    if ('no dual feasible solution' in o
        or 'lp has no dual feasible solution' in o): return 'UNBOUNDED_OR_INFEASIBLE'
    if 'optimal' in o: return 'OPTIMAL'
    return '?'

def main():
    base = '/tmp/difftest'
    os.makedirs(base, exist_ok=True)
    n_tests = int(sys.argv[1]) if len(sys.argv)>1 else 150
    den = float(sys.argv[2]) if len(sys.argv)>2 else 0.4
    opt=inf=unb=0; bad_obj=[]; bad_feas=[]; bad_status=[]; crash=0
    for seed in range(1, n_tests+1):
        n = (seed % 5)*6 + 4
        m = (seed % 4)*4 + 3
        lp_path = f"{base}/p.lp"; mod_path = f"{base}/p.mod"
        gen.gen(n, m, den, seed, base + '/p')
        rc, out, err = run(['./lpsolve', lp_path, '--print'])
        if rc != 0: crash += 1; continue
        myst = None
        for ln in out.splitlines():
            ln=ln.strip()
            if ln.startswith('status:'): myst=ln.split()[1]
            if ln.startswith('objective:'): myobj=float(ln.split()[1])
        rc2, gout, gerr = run(['glpsol', '-m', mod_path])
        gst = glpk_status(gout)
        gobj = None
        for ln in gout.splitlines():
            if ln.startswith('OBJ'): gobj = float(ln.split()[1])
        if myst=='OPTIMAL' and gst=='OPTIMAL':
            if abs(myobj-gobj) <= 1e-5*max(1,abs(gobj)): opt += 1
            else:
                bad_obj.append((seed, myobj, gobj))
        elif gst=='UNBOUNDED_OR_INFEASIBLE' and myst in ('UNBOUNDED','INFEASIBLE'):
            if myst=='UNBOUNDED': unb += 1
            else: inf += 1
        elif myst==gst:
            if myst=='OPTIMAL': opt+=1
            elif myst=='INFEASIBLE': inf+=1
            elif myst=='UNBOUNDED': unb+=1
        else:
            bad_status.append((seed, myst, gst))
        # primal feasibility of my solution
        if myst=='OPTIMAL':
            out_path = f"{base}/out.txt"
            with open(out_path,'w') as f: f.write(out)
            xvals, _ = parse_x(out_path, n)
            ok,res = check(read_lp(lp_path), xvals)
            if not ok: bad_feas.append((seed, res[:2]))
    print(f"OPTIMAL_AGREE={opt} INFEASIBLE_AGREE={inf} UNBOUNDED_AGREE={unb}")
    print(f"OBJ_MISMATCH={len(bad_obj)} STATUS_MISMATCH={len(bad_status)} INFEASIBLE_SOL={len(bad_feas)} CRASH={crash}")
    for x in bad_obj[:15]: print("  OBJ seed=%d mine=%g glpk=%g"%x)
    for x in bad_status[:15]: print("  STATUS seed=%d mine=%s glpk=%s"%x)
    for x in bad_feas[:10]: print("  FEAS seed=%d %s"%x)

main()
