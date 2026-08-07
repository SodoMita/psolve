#!/usr/bin/env python3
"""Focused FlatZinc regression tests with no MiniZinc/Gecode dependency.

Covers exact integer strict/reified relations, the continuous linear float
subset, declaration/index handling, and honest UNKNOWN statuses for forms that
cannot be represented by a closed LP.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOLVER = ROOT / "fznsolve"


def run_model(source: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as f:
        f.write(source)
        path = pathlib.Path(f.name)
    try:
        result = subprocess.run(
            [str(SOLVER), str(path)], text=True, capture_output=True, timeout=10
        )
    finally:
        path.unlink(missing_ok=True)
    if result.returncode != 0:
        raise AssertionError(
            f"fznsolve exited {result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def test_integer_strict() -> None:
    out = run_model(
        """
        var 0..3: x :: output_var;
        constraint int_lt(x, 3);
        constraint int_gt(x, 0);
        solve maximize x;
        """
    )
    require("x = 2;" in out, f"strict scalar relation was relaxed:\n{out}")

    out = run_model(
        """
        var 0..3: x :: output_var;
        constraint int_lin_lt([1], [x], 3);
        solve maximize x;
        """
    )
    require("x = 2;" in out, f"strict linear relation was relaxed:\n{out}")

    out = run_model(
        """
        var 0..10: x :: output_var;
        constraint int_times(2, 3, x);
        solve satisfy;
        """
    )
    require("x = 6;" in out, f"constant multiplication was not enforced:\n{out}")


def test_integer_reification_truth_table() -> None:
    predicates = {
        "int_eq_reif": lambda a, b: a == b,
        "int_le_reif": lambda a, b: a <= b,
        "int_lt_reif": lambda a, b: a < b,
        "int_ge_reif": lambda a, b: a >= b,
        "int_gt_reif": lambda a, b: a > b,
    }
    for predicate, relation in predicates.items():
        for a in range(3):
            for b in range(3):
                for wanted in (False, True):
                    out = run_model(
                        f"""
                        var 0..2: x;
                        var 0..2: y;
                        var bool: r;
                        constraint {predicate}(x, y, r);
                        constraint int_eq(x, {a});
                        constraint int_eq(y, {b});
                        constraint bool_eq(r, {'true' if wanted else 'false'});
                        solve satisfy;
                        """
                    )
                    solved = "----------" in out
                    expected = relation(a, b) == wanted
                    require(
                        solved == expected,
                        f"{predicate}({a}, {b}, {wanted}) expected {expected}, got:\n{out}",
                    )

    # Exercise the linear-form constant path in the big-M encoding as well.
    out = run_model(
        """
        var 0..2: x;
        var 0..2: y;
        var bool: r;
        constraint int_gt_reif(x + 1, y, r);
        constraint int_eq(x, 0);
        constraint int_eq(y, 0);
        constraint bool_eq(r, true);
        solve satisfy;
        """
    )
    require("----------" in out, f"reification lost linear constants:\n{out}")

    out = run_model(
        """
        var bool: a :: output_var;
        var bool: b :: output_var;
        var bool: r :: output_var;
        constraint bool_eq_reif(a, b, r);
        constraint bool_eq(a, true);
        constraint bool_eq(b, false);
        solve satisfy;
        """
    )
    require("r = false;" in out, f"bool equality reification failed:\n{out}")


def test_float_linear_subset() -> None:
    out = run_model(
        """
        array [1..2] of var -5.0..5.0: x :: output_array([1..2]);
        constraint float_lin_eq([1.5, -1.0], x, 0.0);
        constraint float_lin_ge([1.0, 0.0], x, 1.0);
        solve minimize x[2] + 2.5;
        """
    )
    require("x = array1d(1..2, [1, 1.5]);" in out, f"wrong float solution/output:\n{out}")
    require("%%mzn-stat: objective=4" in out, f"objective constant was lost:\n{out}")

    out = run_model(
        """
        var -5.0..5.0: x :: output_var;
        var -5.0..5.0: y :: output_var;
        var -5.0..5.0: z :: output_var;
        constraint float_div(x, 2.0, y);
        constraint float_neg(y, z);
        constraint float_eq(x, 3.0);
        solve satisfy;
        """
    )
    require("x = 3;" in out and "y = 1.5;" in out and "z = -1.5;" in out,
            f"float arithmetic linearization failed:\n{out}")

    out = run_model(
        """
        var float: x :: 0.25..1.75 :: output_var;
        constraint float_eq(x, 1+0.5);
        solve satisfy;
        """
    )
    require("x = 1.5;" in out, f"decimal annotation domain was parsed incorrectly:\n{out}")


def test_table_and_constant_aliases() -> None:
    out = run_model(
        """
        predicate gecode_table_int(array [int] of var int: x, array [int] of int: t);
        array [1..6] of int: tuples = [1,2,2,3,3,1];
        var 2..3: x1 :: output_var;
        var 1..3: x2 :: output_var;
        array [1..2] of var int: x :: output_array([1..2]) = [x1,x2];
        constraint gecode_table_int(x, tuples);
        solve maximize x2;
        """
    )
    require("x = array1d(1..2, [2, 3]);" in out and "objective=3" in out,
            f"table encoding did not select an exact optimum tuple:\n{out}")

    # MiniZinc constant propagation can turn a var-array view into literals.
    # [1,3] combines values that occur in the table but is not a table row.
    out = run_model(
        """
        predicate gecode_table_int(array [int] of var int: x, array [int] of int: t);
        array [1..6] of int: tuples = [1,2,2,3,3,1];
        array [1..2] of var int: x :: output_array([1..2]) = [1,3];
        constraint gecode_table_int(x, tuples);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" in out,
            f"table was weakened to independent column membership:\n{out}")

    out = run_model(
        """
        predicate gecode_table_int(array [int] of var int: x, array [int] of int: t);
        array [1..6] of int: tuples = [1,2,2,3,3,1];
        array [1..2] of var int: x :: output_array([1..2]) = [1,2];
        constraint gecode_table_int(x, tuples);
        solve satisfy;
        """
    )
    require("x = array1d(1..2, [1, 2]);" in out and "----------" in out,
            f"constant var-array alias/output failed:\n{out}")

    out = run_model(
        """
        array [1..4] of int: tuples = [1,2,2,1];
        array [1..2] of var int: x :: output_array([1..2]) = [2,1];
        constraint fzn_table_int(x, tuples);
        solve satisfy;
        """
    )
    require("----------" in out, f"standard fzn_table_int variant failed:\n{out}")


def test_circuit() -> None:
    out = run_model(
        """
        predicate gecode_circuit(int: offset, array [int] of var int: x);
        array [1..4] of var int: s :: output_array([1..4]) = [2,3,4,1];
        constraint gecode_circuit(1, s);
        solve satisfy;
        """
    )
    require("s = array1d(1..4, [2, 3, 4, 1]);" in out and "----------" in out,
            f"valid Hamiltonian circuit was rejected:\n{out}")

    # Two 2-cycles satisfy assignment/all-different but are not one circuit.
    out = run_model(
        """
        predicate gecode_circuit(int: offset, array [int] of var int: x);
        array [1..4] of var int: s :: output_array([1..4]) = [2,1,4,3];
        constraint gecode_circuit(1, s);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" in out,
            f"circuit encoding allowed disconnected subtours:\n{out}")

    out = run_model(
        """
        predicate gecode_circuit(int: offset, array [int] of var int: x);
        array [1..4] of var int: s :: output_array([1..4]) = [1,2,3,0];
        constraint gecode_circuit(0, s);
        solve satisfy;
        """
    )
    require("s = array1d(1..4, [1, 2, 3, 0]);" in out and "----------" in out,
            f"offset-zero circuit was not handled:\n{out}")

    out = run_model(
        """
        array [1..3] of var int: s :: output_array([1..3]) = [2,3,1];
        constraint fzn_circuit(s);
        solve satisfy;
        """
    )
    require("----------" in out, f"standard fzn_circuit variant failed:\n{out}")


def test_declaration_indices_and_honest_unknown() -> None:
    out = run_model(
        """
        var int: x :: 1..3 :: output_var;
        solve minimize x;
        """
    )
    require("x = 1;" in out, f"integer annotation domain was parsed incorrectly:\n{out}")

    out = run_model(
        """
        int: n = 3;
        array [0..n] of var 0..5: x :: output_array([0..3]);
        constraint int_lin_eq([1,1,1,1], x, 6);
        solve maximize x[0];
        """
    )
    require("x = array1d(0..3," in out and "----------" in out,
            f"array extent/index parsing failed:\n{out}")

    out = run_model(
        """
        var 0.0..1.0: x :: output_var;
        constraint float_lt(x, 0.5);
        solve maximize x;
        """
    )
    require("=====UNKNOWN=====" in out,
            f"strict continuous relation must not be relaxed to <=:\n{out}")

    out = run_model(
        """
        var float: x :: output_var;
        solve maximize x;
        """
    )
    require("=====UNKNOWN=====" in out,
            f"synthetic +/-1e9 bound was reported as an optimum:\n{out}")


def main() -> int:
    if not SOLVER.exists():
        print(f"missing solver binary: {SOLVER}", file=sys.stderr)
        return 2
    try:
        test_integer_strict()
        test_integer_reification_truth_table()
        test_float_linear_subset()
        test_table_and_constant_aliases()
        test_circuit()
        test_declaration_indices_and_honest_unknown()
    except AssertionError as exc:
        print(f"FlatZinc semantics test FAILED: {exc}", file=sys.stderr)
        return 1
    print("FlatZinc semantics: strict/reified int + float + table/circuit subset PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
