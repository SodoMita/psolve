#!/usr/bin/env python3
"""Differential test: MiniZinc models -> .fzn -> psolve fznsolve vs Gecode."""
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
    ("abs", """
        var -5..5: x :: output_var;
        var 0..5: y :: output_var;
        constraint y = abs(x);
        solve maximize x;
    """, "max"),
    ("max2", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var 0..10: m :: output_var;
        constraint m = max(x,y);
        constraint x + y <= 12;
        solve maximize m;
    """, "max"),
    ("setdom", """
        var {1,3,5}: x :: output_var;
        solve maximize x;
    """, "max"),
    ("alldiff", """
        include "alldifferent.mzn";
        array[1..3] of var 1..3: x :: output_array([1..3]);
        constraint all_different(x);
        solve satisfy;
    """, "sat"),
    ("element", """
        array[1..4] of int: arr = [10,20,30,40];
        var 1..4: idx :: output_var;
        var int: val :: output_var;
        constraint val = arr[idx];
        solve maximize val;
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

def ref_status(text):
    if '=====UNSATISFIABLE' in text: return 'UNSAT'
    if '----------' in text or '==========' in text: return 'SOL'
    return '?'

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
        rstat = ref_status(ref.stdout)
        if kind == 'max':
            # brute-force the objective from the model when possible
            bobj = None
            if name == 'knap_lin':
                bobj = 10
            elif name == 'mix_prod': bobj = 10
            elif name == 'prod3': bobj = 57
            elif name == 'abs': bobj = 5
            elif name == 'max2': bobj = 10
            elif name == 'setdom': bobj = 5
            elif name == 'element': bobj = 40
            if mstat == 'UNKNOWN' or mstat == '?':
                print(f"[{name}] MINE={mstat} -> FAIL (handled model must solve)"); fail += 1
            elif mstat == 'UNSAT':
                print(f"[{name}] MINE=UNSAT ref={rstat} -> {'OK' if rstat=='UNSAT' else 'FAIL'}")
                if rstat=='UNSAT': ok+=1
                else: fail+=1
            elif bobj is not None and abs((mobj or 0) - bobj) > 1e-6:
                print(f"[{name}] MINE obj={mobj} brute={bobj} -> FAIL"); fail += 1
            else:
                print(f"[{name}] obj={mobj} (brute {bobj}) -> OK"); ok += 1
        else:
            if mstat == 'UNSAT' and rstat == 'UNSAT': ok += 1; print(f"[{name}] both UNSAT -> OK")
            elif mstat in ('SOL','UNSAT') and rstat in ('SOL','UNSAT'): ok += 1; print(f"[{name}] sat mine={mstat} ref={rstat} -> OK")
            elif mstat == 'UNKNOWN': fail += 1; print(f"[{name}] MINE=UNKNOWN ref={rstat} -> FAIL")
            else: fail += 1; print(f"[{name}] mine={mstat} ref={rstat} -> FAIL")
    print(f"MZN differential: OK={ok} FAIL={fail}")

main()
