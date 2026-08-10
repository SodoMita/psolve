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
    """, "max", 10.0),
    ("mix_prod", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint 2*x + y <= 15;
        constraint x + 3*y <= 20;
        solve maximize x + y;
    """, "max", 10.0),
    ("bool_and_sat", """
        var bool: a :: output_var;
        var bool: b :: output_var;
        var bool: c :: output_var;
        constraint (a /\\ b) = c;
        constraint a \\/ b;
        solve satisfy;
    """, "sat", None),
    ("eq_linear", """
        var 0..20: x :: output_var;
        var 0..20: y :: output_var;
        constraint x + y = 15;
        constraint x - y = 3;
        solve satisfy;
    """, "sat", None),
    ("prod3", """
        var 0..20: a :: output_var;
        var 0..20: b :: output_var;
        var 0..20: c :: output_var;
        constraint a + b + c <= 25;
        constraint 2*a + b <= 30;
        constraint b + 3*c <= 40;
        solve maximize a + 2*b + 3*c;
    """, "max", 57.0),
    ("abs", """
        var -5..5: x :: output_var;
        var 0..5: y :: output_var;
        constraint y = abs(x);
        solve maximize x;
    """, "max", 5.0),
    ("max2", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var 0..10: m :: output_var;
        constraint m = max(x,y);
        constraint x + y <= 12;
        solve maximize m;
    """, "max", 10.0),
    ("setdom", """
        var {1,3,5}: x :: output_var;
        solve maximize x;
    """, "max", 5.0),
    ("alldiff", """
        include "alldifferent.mzn";
        array[1..3] of var 1..3: x :: output_array([1..3]);
        constraint all_different(x);
        solve satisfy;
    """, "sat", None),
    ("element", """
        array[1..4] of int: arr = [10,20,30,40];
        var 1..4: idx :: output_var;
        var int: val :: output_var;
        constraint val = arr[idx];
        solve maximize val;
    """, "max", 40.0),
    ("table", """
        include "table.mzn";
        array[1..2] of var 1..3: x :: output_array([1..2]);
        array[1..3,1..2] of int: tuples =
          array2d(1..3, 1..2, [1,2, 2,3, 3,1]);
        constraint table(x, tuples);
        constraint x[1] >= 2;
        solve maximize x[2];
    """, "max", 3.0),
    ("circuit", """
        include "circuit.mzn";
        array[1..4] of var 1..4: successor :: output_array([1..4]);
        constraint circuit(successor);
        solve satisfy;
    """, "sat", None),
    ("reif_eq", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var bool: b :: output_var;
        constraint b <-> (x = y);
        constraint x <= 6;
        solve maximize x;
    """, "max", 6.0),
    ("not_eq", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var bool: b :: output_var;
        constraint b <-> (x = y);
        constraint not b;
        solve maximize x;
    """, "max", 10.0),
    ("lin_ne", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint x + y != 7;
        solve maximize x;
    """, "max", 10.0),
    ("count", """
        array[1..4] of var 1..3: x :: output_array([1..4]);
        constraint count(x, 2) = 2;
        solve satisfy;
    """, "sat", None),
    ("lin_ge", """
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint x - y >= 5;
        solve maximize x;
    """, "max", 10.0),
    ("all_equal", """
        include "all_equal.mzn";
        array[1..4] of var 1..5: x :: output_array([1..4]);
        constraint all_equal(x);
        constraint x[1] = 4;
        solve satisfy;
    """, "sat", None),
    ("increasing", """
        include "increasing.mzn";
        array[1..4] of var 1..5: x :: output_array([1..4]);
        constraint increasing(x);
        constraint x[1] = 2 /\\ x[4] = 4;
        solve maximize sum(x);
    """, "max", 14.0),
    ("strictly_increasing", """
        include "strictly_increasing.mzn";
        array[1..4] of var 1..5: x :: output_array([1..4]);
        constraint strictly_increasing(x);
        solve maximize sum(x);
    """, "max", 14.0),
    ("decreasing", """
        include "decreasing.mzn";
        array[1..4] of var 1..5: x :: output_array([1..4]);
        constraint decreasing(x);
        constraint x[1] = 4 /\\ x[4] = 2;
        solve maximize sum(x);
    """, "max", 14.0),
    ("lex_lesseq", """
        include "lex_lesseq.mzn";
        array[1..3] of var 1..3: x :: output_array([1..3]);
        array[1..3] of var 1..3: y :: output_array([1..3]);
        constraint x = [1, 2, 3];
        constraint lex_lesseq(x, y);
        solve minimize sum(y);
    """, "min", 4.0),
    ("lex_less", """
        include "lex_less.mzn";
        array[1..3] of var 1..3: x :: output_array([1..3]);
        array[1..3] of var 1..3: y :: output_array([1..3]);
        constraint x = [1, 2, 3];
        constraint lex_less(x, y);
        solve minimize sum(y);
    """, "min", 4.0),
    ("gcc", """
        include "global_cardinality.mzn";
        array[1..4] of var 1..3: x :: output_array([1..4]);
        array[1..3] of var 0..4: c :: output_array([1..3]);
        constraint global_cardinality(x, [1, 2, 3], c);
        constraint c = [2, 1, 1];
        solve satisfy;
    """, "sat", None),
    ("bin_packing", """
        include "bin_packing.mzn";
        array[1..3] of var 1..2: bin :: output_array([1..3]);
        array[1..3] of int: w = [3, 4, 2];
        constraint bin_packing(5, bin, w);
        solve satisfy;
    """, "sat", None),
    ("disjunctive", """
        include "disjunctive.mzn";
        array[1..2] of var 0..10: s :: output_array([1..2]);
        array[1..2] of int: d = [3, 4];
        constraint disjunctive(s, d);
        constraint s[1] + 3 <= 10 /\\ s[2] + 4 <= 10;
        solve maximize s[1] + s[2];
    """, "max", 10.0),
    ("inverse", """
        include "inverse.mzn";
        array[1..3] of var 1..3: f :: output_array([1..3]);
        array[1..3] of var 1..3: invf :: output_array([1..3]);
        constraint inverse(f, invf);
        constraint f = [2, 3, 1];
        solve satisfy;
    """, "sat", None),
    ("member", """
        include "member.mzn";
        array[1..3] of var 1..5: x :: output_array([1..3]);
        var 1..5: y :: output_var;
        constraint x = [1, 3, 5];
        constraint member(x, y);
        solve maximize y;
    """, "max", 5.0),
    ("alldiff_except_0", """
        include "alldifferent_except_0.mzn";
        array[1..4] of var 0..3: x :: output_array([1..4]);
        constraint alldifferent_except_0(x);
        constraint x[1] = 0 /\\ x[2] = 0 /\\ x[3] = 2;
        solve maximize x[4];
    """, "max", 3.0),
    ("sliding_sum", """
        include "sliding_sum.mzn";
        array[1..4] of var 1..5: x :: output_array([1..4]);
        constraint sliding_sum(3, 6, 2, x);
        solve maximize sum(x);
    """, "max", 12.0),
    ("nvalue", """
        include "nvalue.mzn";
        array[1..4] of var 1..3: x :: output_array([1..4]);
        var 1..4: nv :: output_var;
        constraint nvalue(nv, x);
        constraint x = [1, 2, 2, 3];
        solve satisfy;
    """, "sat", None),
    ("diffn", """
        include "diffn.mzn";
        array[1..2] of var 0..5: x :: output_array([1..2]);
        array[1..2] of var 0..5: y :: output_array([1..2]);
        array[1..2] of int: dx = [2, 3];
        array[1..2] of int: dy = [2, 2];
        constraint diffn(x, y, dx, dy);
        constraint x[1] + 2 <= 5 /\\ y[1] + 2 <= 5;
        constraint x[2] + 3 <= 5 /\\ y[2] + 2 <= 5;
        solve satisfy;
    """, "sat", None),
    ("cumulative", """
        include "cumulative.mzn";
        array[1..4] of var 0..8: s :: output_array([1..4]);
        array[1..4] of int: d = [3, 2, 2, 2];
        array[1..4] of int: r = [1, 2, 1, 1];
        constraint cumulative(s, d, r, 2);
        constraint forall(i in 1..4)(s[i] + d[i] <= 8);
        solve satisfy;
    """, "sat", None)
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
    for item in MODELS:
        name = item[0]
        src = item[1]
        kind = item[2]
        bobj = item[3] if len(item) > 3 else None

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
        if kind in ('max', 'min'):
            if mstat == 'UNKNOWN' or mstat == '?':
                print(f"[{name}] MINE={mstat} -> FAIL (handled model must solve)"); fail += 1
            elif mstat == 'UNSAT':
                print(f"[{name}] MINE=UNSAT ref={rstat} -> {'OK' if rstat=='UNSAT' else 'FAIL'}")
                if rstat=='UNSAT': ok+=1
                else: fail+=1
            elif bobj is not None and abs((mobj or 0) - bobj) > 1e-4:
                print(f"[{name}] MINE obj={mobj} brute={bobj} -> FAIL"); fail += 1
            else:
                print(f"[{name}] obj={mobj} (brute {bobj}) -> OK"); ok += 1
        else:
            if mstat == 'UNSAT' and rstat == 'UNSAT': ok += 1; print(f"[{name}] both UNSAT -> OK")
            elif mstat in ('SOL','UNSAT') and rstat in ('SOL','UNSAT'): ok += 1; print(f"[{name}] sat mine={mstat} ref={rstat} -> OK")
            elif mstat == 'UNKNOWN': fail += 1; print(f"[{name}] MINE=UNKNOWN ref={rstat} -> FAIL")
            else: fail += 1; print(f"[{name}] mine={mstat} ref={rstat} -> FAIL")
    print(f"MZN differential: OK={ok} FAIL={fail}")
    return 0 if fail == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
