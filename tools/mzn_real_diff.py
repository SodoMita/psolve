#!/usr/bin/env python3
import argparse
import subprocess
import os
import sys
import re
import shutil
import tempfile
import glob as globmod
from pathlib import Path

PSOLVE_DIR = os.environ.get('PSOLVE_DIR') or os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))
MZN_CACHED = os.path.expanduser('~/.cache/psolve-mzn')

def find_minizinc():
    env_mzn = os.environ.get('MINIZINC')
    if env_mzn and os.path.isfile(env_mzn) and os.access(env_mzn, os.X_OK):
        return env_mzn
    for pattern in [
        os.path.join(PSOLVE_DIR, 'scratch/mzn/MiniZinc-*/bin/minizinc'),
        os.path.join(MZN_CACHED, 'MiniZinc-*/bin/minizinc'),
    ]:
        matches = globmod.glob(pattern)
        if matches:
            m = sorted(matches)[-1]
            if os.path.isfile(m) and os.access(m, os.X_OK):
                return m
    which = shutil.which('minizinc')
    if which:
        return which
    return None

def build_temp_solver_config(mzn_bin):
    bundle = os.path.dirname(os.path.dirname(mzn_bin))
    stdlib = os.path.join(bundle, 'share/minizinc')
    temp_dir = tempfile.mkdtemp(prefix='mzn_real_diff_')
    solvers_dir = os.path.join(temp_dir, 'solvers')
    psolve_lib_dir = os.path.join(temp_dir, 'psolve')
    os.makedirs(solvers_dir)
    os.makedirs(psolve_lib_dir)
    
    # Copy psolve.msc
    psolve_msc_src = os.path.join(PSOLVE_DIR, 'share/minizinc/solvers/psolve.msc')
    psolve_msc_dst = os.path.join(solvers_dir, 'psolve.msc')
    with open(psolve_msc_src) as f:
        content = f.read()
    content = content.replace('"mznlib": "psolve"', '"mznlib": "../psolve"')
    with open(psolve_msc_dst, 'w') as f:
        f.write(content)
    
    # Copy .mzn files
    psolve_mzn_src = os.path.join(PSOLVE_DIR, 'share/minizinc/psolve')
    for fn in os.listdir(psolve_mzn_src):
        if fn.endswith('.mzn'):
            shutil.copy2(os.path.join(psolve_mzn_src, fn), os.path.join(psolve_lib_dir, fn))
    
    # Copy/symlink fznsolve
    fznsolve_src = os.path.join(PSOLVE_DIR, 'fznsolve')
    fznsolve_dst = os.path.join(solvers_dir, 'fznsolve')
    try:
        os.symlink(fznsolve_src, fznsolve_dst)
    except Exception:
        shutil.copy2(fznsolve_src, fznsolve_dst)
    os.chmod(fznsolve_dst, 0o755)
    
    return temp_dir, bundle, stdlib

def normalize_solutions(output):
    # Split on ----------
    blocks = re.split(r'^----------\s*$', output, flags=re.M)
    solutions = set()
    has_unknown = '=====UNKNOWN=====' in output
    has_unsat = '=====UNSATISFIABLE=====' in output
    for block in blocks:
        # Skip empty
        if not block.strip():
            continue
        lines = []
        for line in block.splitlines():
            s = line.strip()
            if not s:
                continue
            if s.startswith('%') or s.startswith('%%'):
                continue
            if s == '==========' or s == '=====UNSATISFIABLE=====' or s == '=====UNKNOWN=====':
                continue
            lines.append(s)
        if lines:
            # sort lines inside block for normalization
            lines_sorted = '\n'.join(sorted(lines))
            solutions.add(lines_sorted)
    return solutions, has_unknown, has_unsat

def parse_objective(output):
    # Try to find objective
    m = re.search(r'%%mzn-stat: objective=([^\n\r]+)', output)
    if m:
        try:
            return float(m.group(1).strip())
        except Exception:
            pass
    # Try last solution block
    blocks = re.split(r'^----------\s*$', output, flags=re.M)
    for block in reversed(blocks):
        if not block.strip():
            continue
        lines = []
        for line in block.splitlines():
            s = line.strip()
            if not s:
                continue
            if s.startswith('%') or s.startswith('%%'):
                continue
            if s == '==========' or s == '=====UNSATISFIABLE=====' or s == '=====UNKNOWN=====':
                continue
            lines.append(s)
        if lines:
            return '\n'.join(sorted(lines))
    return None

def build_corpus():
    corpus = []
    
    # 1. linear arithmetic sat
    corpus.append(("lin_sat1", "sat", '''include "globals.mzn";
var 0..10: x :: output_var;
var 0..10: y :: output_var;
var 0..10: z :: output_var;
constraint 2*x + 3*y - z = 10;
constraint x + y + z <= 12;
solve satisfy;
'''))
    
    # 2. all_different
    corpus.append(("all_diff_int", "sat", '''include "all_different.mzn";
array[1..4] of var 1..4: x :: output_array([1..4]);
constraint all_different(x);
constraint x[1] = 1;
solve satisfy;
'''))
    
    # 3. element
    corpus.append(("element1", "max", '''include "globals.mzn";
array[1..5] of int: a = [5, 9, 2, 7, 3];
var 1..5: i :: output_var;
var int: v :: output_var;
constraint v = a[i];
solve maximize v;
'''))
    
    # 4. table
    corpus.append(("table1", "max", '''include "table.mzn";
array[1..2] of var 1..4: x :: output_array([1..2]);
array[1..4, 1..2] of int: t = [|1,5| 2,3| 3,7| 4,1|];
constraint table(x, t);
solve maximize x[1]+x[2];
'''))
    
    # 5. circuit
    corpus.append(("circuit1", "sat", '''include "circuit.mzn";
array[1..4] of var 1..4: succ :: output_array([1..4]);
constraint circuit(succ);
constraint succ[1] = 2;
solve satisfy;
'''))
    
    # 6. cumulative
    corpus.append(("cumulative1", "sat", '''include "cumulative.mzn";
array[1..3] of var 0..4: start :: output_array([1..3]);
array[1..3] of int: dur = [2,3,2];
array[1..3] of int: res = [1,1,1];
constraint cumulative(start, dur, res, 2);
solve satisfy;
'''))
    
    # 7. regular
    corpus.append(("regular1", "sat", '''include "regular.mzn";
array[1..5] of var 1..2: x :: output_array([1..5]);
constraint regular(x, 2, 2, [|2,1| 1,2|], 1, {2});
solve satisfy;
'''))
    
    # 8. regular_reif
    corpus.append(("regular_reif1", "sat", '''include "regular.mzn";
array[1..4] of var 1..2: x :: output_array([1..4]);
var bool: b :: output_var;
constraint fzn_regular_reif(x, 2, 2, array2d(1..2,1..2,[2,1,1,2]), 1, {2}, b);
solve satisfy;
'''))
    
    # 9. lex_lesseq
    corpus.append(("lex_lesseq1", "min", '''include "lex_lesseq.mzn";
array[1..3] of var 1..3: x :: output_array([1..3]);
array[1..3] of var 1..3: y :: output_array([1..3]);
constraint x = [1,1,1];
constraint lex_lesseq(x, y);
solve minimize sum(y);
'''))
    
    # 10. lex_less
    corpus.append(("lex_less1", "min", '''include "lex_less.mzn";
array[1..3] of var 1..3: x :: output_array([1..3]);
array[1..3] of var 1..3: y :: output_array([1..3]);
constraint x = [1,2,2];
constraint lex_less(x, y);
solve minimize sum(y);
'''))
    
    # 11. lex2
    corpus.append(("lex2_1", "min", '''include "lex2.mzn";
array[1..2,1..2] of var 1..2: x :: output_array([1..2,1..2]);
constraint lex2(x);
solve minimize sum(i in 1..2, j in 1..2)(x[i,j]);
'''))
    
    # 12. lex_chain_lesseq
    corpus.append(("lex_chain_lesseq1", "sat", '''include "lex_chain_lesseq.mzn";
array[1..3,1..2] of var 1..2: x :: output_array([1..3,1..2]);
constraint lex_chain_lesseq(x);
solve satisfy;
'''))
    
    # 13. value_precede_chain
    corpus.append(("value_precede_chain1", "sat", '''include "value_precede_chain.mzn";
array[1..5] of var 1..3: x :: output_array([1..5]);
constraint value_precede_chain([1,2,3], x);
solve satisfy;
'''))
    
    # 14. value_precede
    corpus.append(("value_precede1", "sat", '''include "value_precede.mzn";
array[1..4] of var 1..3: x :: output_array([1..4]);
constraint value_precede(1, 2, x);
solve satisfy;
'''))
    
    # 15. seq_precede_chain
    corpus.append(("seq_precede_chain1", "sat", '''include "seq_precede_chain.mzn";
array[1..4] of var 1..3: x :: output_array([1..4]);
constraint seq_precede_chain(x);
solve satisfy;
'''))
    
    # 16. arg_min
    corpus.append(("arg_min1", "min", '''include "arg_min.mzn";
array[1..4] of var 1..5: x :: output_array([1..4]);
var 1..4: i :: output_var;
constraint i = arg_min(x);
solve minimize x[i];
'''))
    
    # 17. arg_max
    corpus.append(("arg_max1", "max", '''include "arg_max.mzn";
array[1..4] of var 1..5: x :: output_array([1..4]);
var 1..4: i :: output_var;
constraint i = arg_max(x);
solve maximize x[i];
'''))
    
    # 18. minimum
    corpus.append(("minimum1", "min", '''include "globals.mzn";
array[1..4] of var 1..10: x;
var int: m :: output_var;
constraint m = min(x);
solve minimize m;
'''))
    
    # 19. maximum
    corpus.append(("maximum1", "max", '''include "globals.mzn";
array[1..4] of var 1..10: x;
var int: m :: output_var;
constraint m = max(x);
solve maximize m;
'''))
    
    # 20. nvalue
    corpus.append(("nvalue1", "min", '''include "nvalue.mzn";
array[1..4] of var 1..3: x;
var int: n :: output_var;
constraint nvalue(n, x);
solve minimize n;
'''))
    
    # 21. among
    corpus.append(("among1", "min", '''include "among.mzn";
array[1..5] of var 1..3: x :: output_array([1..5]);
var int: c :: output_var;
constraint among(c, x, {2});
solve minimize c;
'''))
    
    # 22. global_cardinality
    corpus.append(("gcc1", "sat", '''include "global_cardinality.mzn";
array[1..6] of var 1..3: x :: output_array([1..6]);
array[1..3] of int: cover = [1,2,3];
array[1..3] of var 0..6: cards :: output_array([1..3]);
constraint global_cardinality(x, cover, cards);
constraint cards[1]=2 /\\ cards[2]=2 /\\ cards[3]=2;
solve satisfy;
'''))
    
    # 23. bin_packing
    corpus.append(("bin_packing1", "sat", '''include "bin_packing.mzn";
array[1..4] of var 1..2: bin :: output_array([1..4]);
array[1..4] of int: w = [1,1,1,1];
constraint bin_packing(2, bin, w);
solve satisfy;
'''))
    
    # 24. inverse
    corpus.append(("inverse1", "sat", '''include "inverse.mzn";
array[1..4] of var 1..4: f :: output_array([1..4]);
array[1..4] of var 1..4: invf :: output_array([1..4]);
constraint inverse(f, invf);
solve satisfy;
'''))
    
    # 25. sliding_sum
    corpus.append(("sliding_sum1", "sat", '''include "sliding_sum.mzn";
array[1..6] of var 0..1: x :: output_array([1..6]);
constraint sliding_sum(2, 3, 6, x);
solve satisfy;
'''))
    
    # 26. increasing
    corpus.append(("increasing1", "min", '''include "increasing.mzn";
array[1..4] of var 1..4: x :: output_array([1..4]);
constraint increasing(x);
solve minimize sum(x);
'''))
    
    # 27. bool clauses
    corpus.append(("bool_clauses1", "sat", '''include "globals.mzn";
var bool: a :: output_var;
var bool: b :: output_var;
var bool: c :: output_var;
constraint clause([a,b],[c]);
constraint a = true;
solve satisfy;
'''))
    
    # 28. reified linear constraints
    corpus.append(("reif_lin1", "max", '''include "globals.mzn";
var 0..10: x :: output_var;
var bool: b :: output_var;
constraint b <-> (x + 5 >= 10);
constraint x <= 8;
solve maximize x + bool2int(b);
'''))
    
    # 29. set_in
    corpus.append(("set_in1", "sat", '''include "globals.mzn";
var 1..10: x :: output_var;
constraint x in {2,3,5,7};
solve satisfy;
'''))
    
    # 30. int_pow with CONSTANT exponent
    corpus.append(("int_pow_const1", "max", '''include "globals.mzn";
var 0..5: x :: output_var;
var int: y :: output_var;
constraint y = x^3;
solve maximize y;
'''))
    
    # 31. int_pow with VARIABLE exponent
    corpus.append(("int_pow_var1", "max", '''include "globals.mzn";
var 0..4: x :: output_var;
var 0..3: e :: output_var;
var int: y :: output_var;
constraint y = pow(x,e);
constraint e >= 1;
solve maximize y;
'''))
    
    # 32. int_times
    corpus.append(("int_times1", "max", '''include "globals.mzn";
var 0..6: x :: output_var;
var 0..6: y :: output_var;
var int: z :: output_var;
constraint z = x*y;
solve maximize z;
'''))
    
    # 33. int_div
    corpus.append(("int_div1", "sat", '''include "globals.mzn";
var 0..20: x :: output_var;
var int: y :: output_var;
constraint y = x div 3;
constraint y = 4;
solve satisfy;
'''))
    
    # 34. int_mod
    corpus.append(("int_mod1", "sat", '''include "globals.mzn";
var 0..20: x :: output_var;
var int: y :: output_var;
constraint y = x mod 5;
constraint y = 2;
constraint x >= 10;
solve satisfy;
'''))
    
    # 35. plus
    corpus.append(("plus1", "max", '''include "globals.mzn";
var 0..10: x :: output_var;
var 0..10: y :: output_var;
var int: z :: output_var;
constraint z = x + y;
solve maximize z;
'''))
    
    # UNSAT models
    corpus.append(("unsat1", "sat", '''include "globals.mzn";
var 0..5: x :: output_var;
constraint x >= 10;
solve satisfy;
'''))
    
    corpus.append(("unsat2", "sat", '''include "all_different.mzn";
array[1..3] of var 1..2: x :: output_array([1..3]);
constraint all_different(x);
solve satisfy;
'''))
    
    corpus.append(("unsat3", "sat", '''include "globals.mzn";
var 0..3: x :: output_var;
var 0..3: y :: output_var;
constraint x + y >= 10;
constraint x <= 3;
constraint y <= 3;
solve satisfy;
'''))
    
    return corpus

def run_solver(mzn_bin, solver, model_path, env, temp_dir, extra_flags, timeout=60):
    cmd = [mzn_bin, '--solver', solver] + extra_flags + [model_path]
    try:
        proc = subprocess.run(cmd, cwd=temp_dir, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              timeout=timeout)
        return proc.stdout.decode('utf-8', errors='replace') + proc.stderr.decode('utf-8', errors='replace')
    except subprocess.TimeoutExpired:
        return 'TIMEOUT\n'
    except Exception as e:
        return f'ERR {e}\n'


def best_objective(output, kind):
    """Parse `_objective = <num>;` lines emitted by --output-objective and
    return the optimal (min or max).  Returns None if no objective printed."""
    vals = []
    for m in re.finditer(r'_objective\s*=\s*(-?\d+(?:\.\d+)?)\s*;', output):
        try:
            vals.append(float(m.group(1)))
        except ValueError:
            pass
    if not vals:
        return None
    return (min(vals) if kind == 'min' else max(vals))


def main():
    if '--list' in sys.argv:
        for name, kind, src in build_corpus():
            print(name)
        return 0

    # Require a complete psolve tree before doing any work, so a bad
    # PSOLVE_DIR (or running the script from an unrelated checkout) skips
    # gracefully instead of raising FileNotFoundError.
    for required in ('share/minizinc/solvers/psolve.msc',
                     'share/minizinc/psolve',
                     'fznsolve'):
        if not os.path.exists(os.path.join(PSOLVE_DIR, required)):
            print(f'SKIP: psolve tree incomplete ({required} missing)')
            return 0

    seed = None
    limit = None
    nums = []
    for arg in sys.argv[1:]:
        if arg == '--list':
            continue
        try:
            nums.append(int(arg))
        except ValueError:
            pass
    if nums:
        limit = nums[0]
    if len(nums) > 1:
        seed = nums[1]

    mzn_bin = find_minizinc()
    if not mzn_bin:
        print('SKIP: no MiniZinc found')
        return 0

    temp_dir = None
    try:
        temp_dir, bundle, stdlib = build_temp_solver_config(mzn_bin)
        env = os.environ.copy()
        env['MZN_SOLVER_PATH'] = (os.path.join(temp_dir, 'solvers') + os.pathsep +
                                  os.path.join(bundle, 'share/minizinc/solvers'))
        env['MZN_STDLIB_DIR'] = stdlib
        env['PATH'] = os.path.join(temp_dir, 'solvers') + os.pathsep + env.get('PATH', '')

        corpus = build_corpus()
        if limit is not None:
            corpus = corpus[:limit]

        ok = wrong = unknown = skip = 0

        for name, kind, src in corpus:
            model_path = os.path.join(temp_dir, f'{name}.mzn')
            try:
                with open(model_path, 'w') as f:
                    f.write(src)
            except Exception:
                print(f'[{name}] SKIP (write failed)')
                skip += 1
                continue

            # Satisfy models: enumerate ALL solutions and compare exact sets.
            # Optimize models: compare only the optimum, via an explicit
            # _objective line (both solvers print it with --output-objective).
            if kind in ('min', 'max'):
                flags = ['--output-objective']
            else:
                flags = ['-a']

            out_ps = run_solver(mzn_bin, 'org.psolve.fznsolve', model_path, env, temp_dir, flags)
            out_gc = run_solver(mzn_bin, 'org.gecode.gecode', model_path, env, temp_dir, flags)

            if out_ps.startswith('TIMEOUT') or out_gc.startswith('TIMEOUT'):
                print(f'[{name}] SKIP (timeout)')
                skip += 1
                continue

            sol_ps, unk_ps, unsat_ps = normalize_solutions(out_ps)
            sol_gc, unk_gc, unsat_gc = normalize_solutions(out_gc)

            # A compile error on either side is not a solver verdict.
            def compile_fail(o):
                return ('Error:' in o or 'error:' in o) and '=====' not in o and '----------' not in o
            if compile_fail(out_ps) or compile_fail(out_gc):
                print(f'[{name}] SKIP (compile)')
                skip += 1
                continue

            if unk_ps:
                print(f'[{name}] UNKNOWN (honest)')
                unknown += 1
                continue

            if unsat_ps and unsat_gc:
                print(f'[{name}] OK')
                ok += 1
                continue
            if unsat_ps != unsat_gc:
                print(f'[{name}] WRONG (UNSAT mismatch: ps={unsat_ps} gc={unsat_gc})')
                wrong += 1
                continue

            if kind in ('min', 'max'):
                o_ps = best_objective(out_ps, kind)
                o_gc = best_objective(out_gc, kind)
                if o_ps is None or o_gc is None:
                    # Fall back to exact solution-set equality of final blocks.
                    if sol_ps != sol_gc:
                        print(f'[{name}] WRONG (no objective + set mismatch)')
                        wrong += 1
                    else:
                        print(f'[{name}] OK')
                        ok += 1
                elif abs(o_ps - o_gc) > 1e-9:
                    print(f'[{name}] WRONG (objective ps={o_ps} gc={o_gc})')
                    wrong += 1
                else:
                    print(f'[{name}] OK')
                    ok += 1
            else:
                if sol_ps != sol_gc:
                    print(f'[{name}] WRONG (solution set mismatch: ps={len(sol_ps)} gc={len(sol_gc)})')
                    wrong += 1
                else:
                    print(f'[{name}] OK')
                    ok += 1

        print(f'MZN real differential: OK={ok} WRONG={wrong} UNKNOWN={unknown} SKIP={skip}')
        return 1 if wrong > 0 else 0
    finally:
        if temp_dir and os.path.exists(temp_dir):
            try:
                shutil.rmtree(temp_dir)
            except Exception:
                pass


if __name__ == '__main__':
    sys.exit(main())
