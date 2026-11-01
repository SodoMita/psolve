#!/usr/bin/env python3
"""Exhaustive and seeded Boolean FlatZinc differential against a truth-table oracle.

No external solver is required: all assignments are exhaustively evaluated in
Python, then compared to fznsolve's complete -a output, including UNSAT.
"""
import itertools
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

SOLVER = Path(os.environ.get('FZNSOLVE', Path(__file__).resolve().parent.parent / 'fznsolve'))
OPS = {'bool_and': lambda a, b: a and b,
       'bool_or': lambda a, b: a or b,
       'bool_xor': lambda a, b: a != b}


def check(constraints, declarations, names):
    source = declarations + '\n' + '\n'.join(
        f'constraint {op}({a}, {b}, {r});' for op, a, b, r in constraints
    ) + '\nsolve satisfy;\n'
    expected = set()
    for values in itertools.product((False, True), repeat=len(names)):
        env = dict(zip(names, values), false=False, true=True, yes=True, no=False)
        if all(OPS[op](env[a], env[b]) == env[r] for op, a, b, r in constraints):
            expected.add(values)
    with tempfile.NamedTemporaryFile('w', suffix='.fzn') as f:
        f.write(source); f.flush()
        result = subprocess.run([str(SOLVER), '-a', f.name], text=True,
                                capture_output=True, timeout=10)
    out = result.stdout
    if result.returncode or '=====UNKNOWN=====' in out:
        raise AssertionError(f'{source}\nreturn={result.returncode}\n{out}\n{result.stderr}')
    actual = []
    for block in out.split('----------')[:-1]:
        matches = dict(re.findall(r'\b(\w+)\s*=\s*(true|false);', block))
        if set(matches) != set(names):
            raise AssertionError(f'missing output variables: {source}\n{out}')
        actual.append(tuple(matches[name] == 'true' for name in names))
    if (set(actual) != expected or len(actual) != len(expected) or
            ('=====UNSATISFIABLE=====' in out) != (not expected) or
            (bool(expected) and '==========' not in out)):
        raise AssertionError(f'{source}\nexpected={expected} actual={actual}\n{out}\n{result.stderr}')


def main():
    # Every operator and every possible mix of constants, two distinct vars,
    # repeated operands/results, including named Boolean parameter aliases.
    decl = 'var bool: x :: output_var; var bool: y :: output_var;\nbool: yes = true; bool: no = false;'
    terms = ('false', 'true', 'x', 'y')
    count = 0
    for op in OPS:
        for args in itertools.product(terms, repeat=3):
            check([(op, *args)], decl, ('x', 'y')); count += 1
    # All constant models have no decision variable/dummy-output dependence.
    for op in OPS:
        for args in itertools.product(('no', 'yes'), repeat=3):
            check([(op, *args)], 'bool: yes = true; bool: no = false;', ()); count += 1
    rng = random.Random(20261101)
    terms = ('false', 'true', 'yes', 'no', 'x', 'y')
    for _ in range(100):
        cs = [(rng.choice(tuple(OPS)), *(rng.choice(terms) for _ in range(3)))
              for _ in range(rng.randrange(1, 6))]
        check(cs, decl, ('x', 'y')); count += 1
    print(f'fzn_bool_diff: {count} models match exhaustive Boolean truth-table oracle')


if __name__ == '__main__':
    main()
