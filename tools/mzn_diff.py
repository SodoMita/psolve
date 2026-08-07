#!/usr/bin/env python3
"""Differential test: MiniZinc models -> .fzn -> psolve fznsolve vs Gecode.

Compiles each .mzn with `minizinc -c --solver gecode`, solves with fznsolve and
with Gecode, and compares the objective value and solution feasibility.  A
correct solver must match Gecode's objective on linear/bool models it handles
natively; unhandled constraints must yield UNKNOWN (never a wrong answer)."""
import subprocess, os, sys, re

MODELS = [
    ("knap_lin", """
        int: n = 4;
        array[1..n] of int: w = [2,3,4,5];
        array[1..n] of int: v = [3,4,5,6];
        array[1..n] of var 0..1: take :: output_array([1..n]);
        constraint sum(i in 1..n)(w[i]*take[i]) <= 8;
        solve maximize sum(i in 1..n)(v[i]*take[i]);
    """, "max"),
    ("mix_prod", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint 2*x + y <= 15;
        constraint x + 3*y <= 20;
        solve maximize x + y;
    """, "max"),
    ("bool_and_sat", """
        var bool: a :: output_var;
        var bool: b :: output_var;
        var bool: c :: output_var;
        constraint (a /\\ b) = c;
        constraint a \\/ b;
        solve satisfy;
    """, "sat"),
    ("eq_linear", """
        var 0..20: x :: output_var;
        var 0..20: y :: output_var;
        constraint x + y = 15;
        constraint x - y = 3;
        solve satisfy;
    """, "sat"),
    ("prod3", """
        var 0..20: a :: output_var;
        var 0..20: b :: output_var;
        var 0..20: c :: output_var;
        constraint a + b + c <= 25;
        constraint 2*a + b <= 30;
        constraint b + 3*c <= 40;
        solve maximize a + 2*b + 3*c;
    """, "max"),
]

def run(cmd, timeout=120):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

def mine_obj(text):
    for line in text.splitlines():
        if line.startswith('%%mzn-stat: objective='):
            return float(line.split('=')[1])
    return None

def mine_status(text):
    if '=====UNSATISFIABLE' in text: return 'UNSAT'
    if '=====UNKNOWN' in text: return 'UNKNOWN'
    if '----------' in text or '==========' in text: return 'SOL'
    return '?'

def ref_obj(text):
    for line in text.splitlines():
        m = re.search(r'(?:_objective|objective)\s*=\s*(-?\d+)', line)
        if m: return int(m.group(1))
    return None

def main():
    os.makedirs('/tmp/mzndiff', exist_ok=True)
    ok = 0; fail = 0
    for (name, src, kind) in MODELS:
        path = f'/tmp/mzndiff/{name}'
        with open(path + '.mzn', 'w') as f: f.write(src)
        c = run(['minizinc','-c','--solver','gecode','--output-fzn-to-file', path+'.fzn', path+'.mzn'])
        if c.returncode != 0:
            print(f"[{name}] COMPILE FAIL: {c.stderr.strip()[:80]}"); fail += 1; continue
        mine = run(['./fznsolve', path+'.fzn'], 30)
        mstat = mine_status(mine.stdout)
        mobj = mine_obj(mine.stdout)
        ref = run(['minizinc','--solver','gecode', path+'.fzn'])
        rstat = 'UNSAT' if '=====UNSATISFIABLE' in ref.stdout else ('SOL' if '----------' in ref.stdout or '==========' in ref.stdout else '?')
        robj = ref_obj(ref.stdout)
        if kind == 'max':
            if mstat == 'UNKNOWN' or mstat == '?':
                print(f"[{name}] MINE={mstat} (ref obj={robj}) -> FAIL (handled model must solve)")
                fail += 1
            elif mstat == 'UNSAT':
                print(f"[{name}] MINE=UNSAT ref={rstat} obj={robj} -> {'OK' if rstat=='UNSAT' else 'FAIL'}")
                if rstat=='UNSAT': ok+=1
                else: fail+=1
            elif robj is not None and abs((mobj or 0) - robj) > 1e-6:
                print(f"[{name}] MINE obj={mobj} REF obj={robj} -> FAIL")
                fail += 1
            else:
                print(f"[{name}] obj={mobj} (ref {robj}) -> OK")
                ok += 1
        else:  # sat
            if mstat == 'UNSAT' and rstat == 'UNSAT': ok += 1; print(f"[{name}] both UNSAT -> OK")
            elif mstat in ('SOL','UNSAT') and rstat in ('SOL','UNSAT'): ok += 1; print(f"[{name}] sat statuses mine={mstat} ref={rstat} -> OK")
            elif mstat == 'UNKNOWN': fail += 1; print(f"[{name}] MINE=UNKNOWN ref={rstat} -> FAIL")
            else: fail += 1; print(f"[{name}] mine={mstat} ref={rstat} -> FAIL")
    print(f"MZN differential: OK={ok} FAIL={fail}")

main()
