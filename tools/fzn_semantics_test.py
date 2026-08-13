#!/usr/bin/env python3
"""Focused FlatZinc regression tests with no MiniZinc/Gecode dependency.

Covers exact integer strict/reified relations, the continuous linear float
subset, declaration/index handling, and honest UNKNOWN statuses for forms that
cannot be represented by a closed LP.
"""

from __future__ import annotations

import itertools
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
# FZNSOLVE env override: used to prove these regressions discriminate against
# a known-buggy binary (e.g. FZNSOLVE=/tmp/fzn_remote python3 tools/fzn_semantics_test.py)
SOLVER = pathlib.Path(os.environ.get("FZNSOLVE", str(ROOT / "fznsolve")))


def run_model(source: str, *options: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as f:
        f.write(source)
        path = pathlib.Path(f.name)
    try:
        result = subprocess.run(
            [str(SOLVER), *options, str(path)], text=True, capture_output=True, timeout=10
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


def test_array_extrema() -> None:
    out = run_model(
        """
        var 0..5: a;
        var 0..5: b;
        var 0..10: mx :: output_var;
        var 0..10: mn :: output_var;
        array [1..2] of var int: x = [a,b];
        constraint int_eq(a,2);
        constraint int_eq(b,3);
        constraint array_int_maximum(mx,x);
        constraint array_int_minimum(mn,x);
        solve satisfy;
        """
    )
    require("mx = 3;" in out and "mn = 2;" in out,
            f"array extrema selector encoding failed:\n{out}")

    out = run_model(
        """
        var 0..5: a;
        var 0..5: b;
        var 0..10: mx :: output_var;
        array [1..2] of var int: x = [a,b];
        constraint int_ge(a,2);
        constraint int_ge(b,3);
        constraint array_int_maximum(mx,x);
        solve minimize mx;
        """
    )
    require("mx = 3;" in out and "objective=3" in out,
            f"objective-tightened array maximum failed:\n{out}")


def test_integer_reification_truth_table() -> None:
    predicates = {
        "int_eq_reif": lambda a, b: a == b,
        "int_ne_reif": lambda a, b: a != b,
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

    out = run_model(
        """
        var 0..5: x;
        var 0..5: y;
        constraint int_ne(x, 3);
        constraint int_ne(y, 3);
        solve maximize x + y;
        """
    )
    require("----------" in out, f"int_ne scalar failed:\n{out}")

    out = run_model(
        """
        var 0..5: x;
        var bool: r :: output_var;
        constraint int_lin_le_reif([1], [x], 2, r);
        constraint int_eq(x, 4);
        solve satisfy;
        """
    )
    require("r = false;" in out, f"int_lin_le_reif failed:\n{out}")


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


def test_set_membership_constants() -> None:
    """set_in / set_in_reif with constant (or affine) LHS must fold exactly.

    Regression: the reified encodings built the difference form with
    `d.constant = -v` instead of `-= v`, silently dropping the LHS constant,
    so e.g. set_in_reif(7, {3,7,9}, r) bound r = false.  The non-reified
    constant path was UNHANDLED (UNKNOWN) although decidable at compile time.
    """
    sets = [
        ("{3, 7, 9}", {3, 7, 9}),
        ("2..6", set(range(2, 7))),
        ("{0, 5}", {0, 5}),
        ("{}", set()),
        ("1..0", set()),      # empty range
    ]
    cs = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 9]
    for stext, members in sets:
        for c in cs:
            member = c in members
            out = run_model(f"constraint set_in({c}, {stext});\nsolve satisfy;")
            solved = "----------" in out
            unsat = "UNSATISFIABLE" in out
            require(solved == member and unsat == (not member),
                    f"set_in({c}, {stext}) member={member}:\\n{out}")
            # reified truth table, exact both ways via bool_eq
            for wanted in (False, True):
                out = run_model(
                    f"""
                    var bool: r;
                    constraint set_in_reif({c}, {stext}, r);
                    constraint bool_eq(r, {'true' if wanted else 'false'});
                    solve satisfy;
                    """
                )
                solved = "----------" in out
                require(solved == (member == wanted),
                        f"set_in_reif({c}, {stext}, r={wanted}) "
                        f"member={member}:\\n{out}")
    # affine LHS must accumulate the constant, not overwrite it
    out = run_model(
        """
        var 0..10: x;
        var bool: r;
        constraint int_eq(x, 4);
        constraint set_in_reif(x + 3, {7}, r);
        constraint bool_eq(r, true);
        solve satisfy;
        """
    )
    require("----------" in out, f"affine set_in_reif(x+3 in {{7}}) should hold:\\n{out}")


def test_constant_arguments() -> None:
    """Par (constant) and affine arguments in scalar/bool global handlers.

    Regressions: bool_clause skipped par literals ([true] was UNSAT); its
    reified encoding inverted negated-literal signs and divided by zero on
    all-par clauses; array_bool_and/or left r unconstrained; subcircuit
    dropped par values AND had an anchor-less MTZ that made every real
    circuit infeasible; int_min/int_max/int_abs rejected constants and
    silently dropped affine constant terms.
    """
    # bool_clause over par literals: OR(pos) or OR(not neg)
    for pos in ([], ["true"], ["false"]):
        for neg in ([], ["true"], ["false"]):
            sat = any(v == "true" for v in pos) or any(v == "false" for v in neg)
            body = f"constraint bool_clause([{', '.join(pos)}], [{', '.join(neg)}]);\nsolve satisfy;\n"
            out = run_model(body)
            require(("----------" in out) == sat,
                    f"bool_clause({pos}, {neg}) sat={sat}:\\n{out}")
    # bool_clause_reif truth table incl. a negated variable literal
    for a in (False, True):
        for b in (False, True):
            clause = a or (not b)
            for want in (False, True):
                out = run_model(
                    f"""
                    var bool: x;
                    var bool: y;
                    var bool: r;
                    constraint bool_clause_reif([x], [y], r);
                    constraint bool_eq(x, {'true' if a else 'false'});
                    constraint bool_eq(y, {'true' if b else 'false'});
                    constraint bool_eq(r, {'true' if want else 'false'});
                    solve satisfy;
                    """
                )
                require(("----------" in out) == (clause == want),
                        f"bool_clause_reif([{a}],[neg {b}], r={want}):\\n{out}")
    # all-par reified clauses pin r exactly (previously NaN / UNKNOWN)
    for pos, neg, truth in ((["true"], [], True), (["false"], ["true"], False),
                            ([], [], False), (["false"], ["false"], True)):
        out = run_model(
            f"""
            var bool: r :: output_var;
            constraint bool_clause_reif([{', '.join(pos)}], [{', '.join(neg)}], r);
            solve satisfy;
            """
        )
        require(f"r = {'true' if truth else 'false'};" in out,
                f"bool_clause_reif([{pos}],[{neg}]) -> {truth}:\\n{out}")
    # array_bool_and/or with par literals
    for pred, arr, truth in (
        ("array_bool_and", ["true", "true"], True),
        ("array_bool_and", ["true", "false"], False),
        ("array_bool_and", [], True),
        ("array_bool_or", ["false", "false"], False),
        ("array_bool_or", ["false", "true"], True),
        ("array_bool_or", [], False),
    ):
        out = run_model(
            f"""
            var bool: r :: output_var;
            constraint {pred}([{', '.join(arr)}], r);
            solve satisfy;
            """
        )
        require(f"r = {'true' if truth else 'false'};" in out,
                f"{pred}({arr}, r) -> {truth}:\\n{out}")
    # int_min/max/abs with constant and affine arguments
    out = run_model("var int: m :: output_var;\nconstraint int_min(4, 7, m);\nsolve satisfy;\n")
    require("m = 4;" in out, f"int_min const:\\n{out}")
    out = run_model("var int: m :: output_var;\nconstraint int_max(4, 7, m);\nsolve satisfy;\n")
    require("m = 7;" in out, f"int_max const:\\n{out}")
    out = run_model("var 0..10: y :: output_var;\nconstraint int_abs(-3, y);\nsolve satisfy;\n")
    require("y = 3;" in out, f"int_abs const:\\n{out}")
    out = run_model("var int: x :: output_var;\nconstraint int_negate(4, x);\nsolve satisfy;\n")
    require("x = -4;" in out, f"int_negate const:\\n{out}")
    out = run_model(
        """
        var 0..4: x;
        var 0..9: m :: output_var;
        constraint int_eq(x, 1);
        constraint int_min(x + 1, 5, m);
        solve satisfy;
        """
    )
    require("m = 2;" in out, f"int_min affine (min(2,5)):\\n{out}")
    out = run_model("constraint int_min(3, 7, 5);\nsolve satisfy;\n")
    require("UNSATISFIABLE" in out, f"int_min wrong const result must be UNSAT:\\n{out}")
    # among with par elements / empty value set
    out = run_model("constraint fzn_among(2, [1, 2, 1], {1});\nsolve satisfy;\n")
    require("----------" in out, f"among par hit:\\n{out}")
    out = run_model("constraint fzn_among(3, [1, 2, 1], {1});\nsolve satisfy;\n")
    require("UNSATISFIABLE" in out, f"among par miss:\\n{out}")
    out = run_model("constraint fzn_among(1, [1, 2], {});\nsolve satisfy;\n")
    require("UNSATISFIABLE" in out, f"among empty set forces n=0:\\n{out}")
    # subcircuit on par arrays: valid subcircuits / invalid double cycle
    for arr, sat in (("[2, 3, 1]", True), ("[1, 2, 3]", True),
                     ("[2, 1, 3]", True), ("[2, 1, 4, 3]", False)):
        out = run_model(f"constraint fzn_subcircuit({arr});\nsolve satisfy;\n")
        require(("----------" in out) == sat, f"fzn_subcircuit({arr}) sat={sat}:\\n{out}")
    # subcircuit enumeration: n=3 has exactly 1 (identity) + 3 (2-cycles) +
    # 2 (3-cycles) = 6 distinct successor mappings
    res = run_model(
        """
        array [1..3] of var 1..3: xs :: output_array([1..3]);
        constraint fzn_subcircuit(xs);
        solve satisfy;
        """,
        "-a",
    )
    blocks = [b for b in res.split("----------") if "xs =" in b]
    vals = {re.search(r"\[([^\]]*)\]", b).group(1).replace(" ", "") for b in blocks}
    require(len(vals) == 6, f"subcircuit n=3 must enumerate 6 mappings, got {len(vals)}:\\n{res}")
    require("==========" in res, f"subcircuit -a completion marker:\\n{res}")


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


def test_all_solutions_and_extended_constraints() -> None:
    # 1. -a satisfaction: enumerate all solutions
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as f:
        f.write(
            """
            var 1..3: x :: output_var;
            solve satisfy;
            """
        )
        path = pathlib.Path(f.name)
    try:
        res = subprocess.run([str(SOLVER), "-a", str(path)], text=True, capture_output=True, timeout=10)
        require("x = 1;" in res.stdout and "x = 2;" in res.stdout and "x = 3;" in res.stdout,
                f"-a satisfaction missed solutions:\n{res.stdout}")
        require("==========" in res.stdout, f"-a satisfaction missing completion marker:\n{res.stdout}")
    finally:
        path.unlink(missing_ok=True)

    # 1b. -a distinctness + exact solution count: x < y on a 3x3 lattice
    #     has exactly 3 solutions ((1,2),(1,3),(2,3)); blocks must be distinct.
    with tempfile.NamedTemporaryFile("w", suffix=".fzn", delete=False) as f:
        f.write(
            """
            var 1..3: x :: output_var;
            var 1..3: y :: output_var;
            constraint int_lt(x, y);
            solve satisfy;
            """
        )
        path = pathlib.Path(f.name)
    try:
        res = subprocess.run([str(SOLVER), "-a", str(path)], text=True, capture_output=True, timeout=10)
        seen = set()
        blocks = [b for b in res.stdout.split("----------") if "x =" in b]
        for b in blocks:
            xm = re.search(r"x = (\d+);", b)
            ym = re.search(r"y = (\d+);", b)
            require(xm and ym, f"-a malformed block:\\n{b}")
            pair = (int(xm.group(1)), int(ym.group(1)))
            require(pair[0] < pair[1], f"-a violated x<y: {pair}")
            require(pair not in seen, f"-a duplicate solution {pair}")
            seen.add(pair)
        require(len(seen) == 3, f"-a must find exactly 3 solutions, found {len(seen)}:\\n{res.stdout}")
        require("==========" in res.stdout, f"-a missing completion marker:\\n{res.stdout}")
    finally:
        path.unlink(missing_ok=True)

    # Auxiliary selector assignments are not distinct FlatZinc solutions.
    out = run_model(
        """
        array [1..2] of int: tuples = [1, 1];
        var 1..1: x :: output_var;
        array [1..1] of var int: xs = [x];
        constraint fzn_table_int(xs, tuples);
        solve satisfy;
        """,
        "-a",
    )
    require(out.count("x = 1;") == 1,
            f"-a printed duplicate visible solutions for auxiliary selectors:\n{out}")

    # A continuous satisfaction space cannot be completely enumerated.
    out = run_model(
        """
        var 0.0..1.0: x :: output_var;
        solve satisfy;
        """,
        "-a",
    )
    require("=====UNKNOWN=====" in out and "==========" not in out,
            f"-a falsely claimed completion of an infinite float domain:\n{out}")

    # 2. array_var_int_element with variables in the array
    out = run_model(
        """
        var 10..10: v1;
        var 20..20: v2;
        var 30..30: v3;
        array [1..3] of var int: arr = [v1, v2, v3];
        var 1..3: idx :: output_var;
        var 0..50: val :: output_var;
        constraint array_var_int_element(idx, arr, val);
        constraint int_eq(idx, 2);
        solve satisfy;
        """
    )
    require("idx = 2;" in out and "val = 20;" in out,
            f"array_var_int_element with variables failed:\n{out}")

    out = run_model(
        """
        var 1..1: idx :: output_var;
        var 0..0: a;
        var 1000000..1000000: b;
        array [1..2] of var int: arr = [a, b];
        constraint array_var_int_element(idx, arr, 0);
        solve satisfy;
        """
    )
    require("idx = 1;" in out,
            f"element's unselected far value was restricted by an undersized big-M:\n{out}")

    # 3. array_bool_element
    out = run_model(
        """
        var 1..3: idx :: output_var;
        var bool: val :: output_var;
        array [1..3] of bool: arr = [false, true, false];
        constraint array_bool_element(idx, arr, val);
        constraint bool_eq(val, true);
        solve satisfy;
        """
    )
    require("idx = 2;" in out and "val = true;" in out,
            f"array_bool_element failed:\n{out}")

    # 4. array_float_element and array_float_maximum
    out = run_model(
        """
        var 1..3: idx :: output_var;
        var float: val :: output_var;
        array [1..3] of float: arr = [1.5, 3.5, 2.0];
        constraint array_float_element(idx, arr, val);
        solve maximize val;
        """
    )
    require("idx = 2;" in out and "val = 3.5;" in out,
            f"array_float_element failed:\n{out}")

    out = run_model(
        """
        var float: mx :: output_var;
        var 0.0..5.0: a;
        var 0.0..5.0: b;
        array [1..2] of var float: arr = [a, b];
        constraint float_eq(a, 2.5);
        constraint float_eq(b, 4.25);
        constraint array_float_maximum(mx, arr);
        solve satisfy;
        """
    )
    require("mx = 4.25;" in out, f"array_float_maximum failed:\n{out}")

    # 5. int_div and int_mod
    out = run_model(
        """
        var 0..20: a :: output_var;
        var 0..20: q :: output_var;
        var 0..20: r :: output_var;
        constraint int_eq(a, 14);
        constraint int_div(a, 4, q);
        constraint int_mod(a, 4, r);
        solve satisfy;
        """
    )
    require("q = 3;" in out and "r = 2;" in out,
            f"int_div / int_mod failed:\n{out}")

    out = run_model(
        """
        var -5..-5: a;
        var -10..10: q :: output_var;
        var -10..10: r :: output_var;
        constraint int_div(a, 2, q);
        constraint int_mod(a, 2, r);
        solve satisfy;
        """
    )
    require("q = -2;" in out and "r = -1;" in out,
            f"negative int_div / int_mod violated truncation semantics:\n{out}")

    out = run_model(
        """
        var 5..5: a;
        var -10..10: q :: output_var;
        var -10..10: r :: output_var;
        constraint int_div(a, -2, q);
        constraint int_mod(a, -2, r);
        solve satisfy;
        """
    )
    require("q = -2;" in out and "r = 1;" in out,
            f"negative-divisor int_div / int_mod semantics failed:\n{out}")

    # 6. int_pow
    out = run_model(
        """
        var 1..100: z :: output_var;
        var 1..10: x;
        constraint int_eq(x, 5);
        constraint int_pow(x, 2, z);
        solve satisfy;
        """
    )
    require("z = 25;" in out, f"int_pow failed:\n{out}")

    # 7. set_in_reif and int_in
    out = run_model(
        """
        var 1..10: x :: output_var;
        var bool: r1 :: output_var;
        var bool: r2 :: output_var;
        constraint int_eq(x, 5);
        constraint set_in_reif(x, 3..7, r1);
        constraint set_in_reif(x, {1, 2, 8}, r2);
        solve satisfy;
        """
    )
    require("r1 = true;" in out and "r2 = false;" in out,
            f"set_in_reif failed:\n{out}")

    # 8. count_leq, count_geq, count_ne
    out = run_model(
        """
        array [1..4] of var 1..3: x :: output_array([1..4]);
        var 0..4: c1 :: output_var;
        constraint int_eq(x[1], 2);
        constraint int_eq(x[2], 2);
        constraint int_eq(x[3], 1);
        constraint int_eq(x[4], 3);
        constraint int_eq(c1, 2);
        constraint fzn_count_eq(x, 2, c1);
        solve satisfy;
        """
    )
    require("----------" in out, f"count constraint failed:\n{out}")

    out = run_model(
        """
        array [1..2] of var 0..2: x :: output_array([1..2]);
        var 2..2: value :: output_var;
        var 2..2: count;
        constraint int_eq(x[1], 2);
        constraint int_eq(x[2], 2);
        constraint fzn_count_eq(x, value, count);
        solve satisfy;
        """
    )
    require("value = 2;" in out and "array1d(1..2, [2, 2])" in out,
            f"count ignored its variable value argument:\n{out}")

    # 9. table_bool
    out = run_model(
        """
        array [1..4] of bool: tuples = [true, false, false, true];
        var bool: b1 :: output_var;
        var bool: b2 :: output_var;
        array [1..2] of var bool: x :: output_array([1..2]) = [b1, b2];
        constraint fzn_table_bool(x, tuples);
        constraint bool_eq(b1, true);
        solve satisfy;
        """
    )
    require("b1 = true;" in out and "b2 = false;" in out,
            f"table_bool failed:\n{out}")


def test_cp_minmax_constant_operands() -> None:
    """int_min/int_max with constant operands through the CP engine.

    Regression: the CP minmax propagator called cp_dmin(cp,-1) on a constant
    first operand (heap underflow) and intersected m with the wrong operand
    bounds, fabricating UNSATISFIABLE for satisfiable models such as
    int_min(5, x, m).  Every case below was UNSAT/crash on the buggy binary.
    """
    # const-first, m free, plain satisfy must be SAT (was fabricated UNSAT).
    out = run_model(
        """
        var 1..10: x :: output_var;
        var 1..10: m :: output_var;
        constraint int_min(5, x, m);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" not in out and "=====UNKNOWN=====" not in out,
            f"int_min(5,x,m) must not be UNSAT/UNKNOWN:\n{out}")
    # exact propagation checks, const first and second, min and max
    cases = [
        ("int_min(5, x, m)",  "int_eq(x, 9)", "m = 5;"),
        ("int_min(5, x, m)",  "int_eq(x, 3)", "m = 3;"),
        ("int_min(x, 5, m)",  "int_eq(x, 9)", "m = 5;"),
        ("int_max(3, x, m)",  "int_eq(x, 2)", "m = 3;"),
        ("int_max(3, x, m)",  "int_eq(x, 7)", "m = 7;"),
        ("int_max(x, 3, m)",  "int_eq(x, 7)", "m = 7;"),
        ("int_max(2, 5, m)",  None,           "m = 5;"),
        ("int_min(2, 5, m)",  None,           "m = 2;"),
        # m pinned above/below forces operand fixing through minmax
        ("int_max(3, x, m)",  "int_eq(m, 8)", "x = 8;"),
        ("int_min(2, x, m)",  "int_eq(m, 1)", "x = 1;"),
    ]
    for cons, extra, expect in cases:
        body = (
            "var 1..10: x :: output_var;\n"
            "var 1..10: m :: output_var;\n"
            f"constraint {cons};\n"
            + (f"constraint {extra};\n" if extra else "")
            + "solve satisfy;\n"
        )
        out = run_model(body)
        require(expect in out, f"{cons} with {extra}: expected {expect}:\n{out}")
    # genuinely infeasible must remain UNSAT (m out of min's reach)
    out = run_model(
        """
        var 1..10: x :: output_var;
        var 1..10: m :: output_var;
        constraint int_min(5, x, m);
        constraint int_eq(m, 7);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" in out,
            f"int_min(5,x,7) is genuinely infeasible but was not reported:\n{out}")
    # MIP bridge path (float var forces fallback): constant operands must
    # materialize, not be dropped.
    out = run_model(
        """
        var 1..10: x :: output_var;
        var 1..10: m :: output_var;
        var 0.0..1.0: f;
        constraint float_eq(f, 0.5);
        constraint int_max(3, x, m);
        constraint int_eq(x, 7);
        solve satisfy;
        """
    )
    require("m = 7;" in out, f"MIP-path int_max(3,x,m) wrong:\n{out}")


def test_fractional_lattice_relations() -> None:
    """Fractional constants inside integer relations / reifications.

    The encodings used to hardcode a +/-1 lattice step around 0.  On a
    fractional lattice (d in g*Z+f) that fabricated UNSAT/wrong reifications
    (int_le_reif(x,0.6,r) with x=1 forced r=true) and the CP parser llround'ed
    coefficients (int_lin_eq([0.6],[x],1) became x=1).  Every answer below is
    checked against exact float semantics.
    """
    # CP must decline, MIP proves 0.6x = 1 has no integer solution.
    out = run_model(
        """
        var 0..2: x :: output_var;
        constraint int_lin_eq([0.6], [x], 1);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" in out,
            f"0.6*x == 1 over integers is UNSAT:\n{out}")

    # Full scalar-reif truth table against float semantics.
    rels = {
        "int_eq_reif": lambda a, b: a == b,
        "int_ne_reif": lambda a, b: a != b,
        "int_le_reif": lambda a, b: a <= b,
        "int_lt_reif": lambda a, b: a < b,
        "int_ge_reif": lambda a, b: a >= b,
        "int_gt_reif": lambda a, b: a > b,
    }
    for xval in (0, 1, 2):
        for name, fn in rels.items():
            expect_r = "true" if fn(xval, 0.6) else "false"
            out = run_model(
                f"""
                var 0..2: x :: output_var;
                var bool: r :: output_var;
                constraint {name}(x, 0.6, r);
                constraint int_eq(x, {xval});
                solve satisfy;
                """
            )
            require(f"r = {expect_r};" in out,
                    f"{name}(x={xval}, 0.6): expected r = {expect_r}:\n{out}")

    # eq/ne on a fractional lattice collapse to constants.
    out = run_model(
        """
        var 0..2: x :: output_var;
        var bool: r :: output_var;
        constraint int_eq_reif(x, 0.6, r);
        constraint bool_eq(r, true);
        solve satisfy;
        """
    )
    require("=====UNSATISFIABLE=====" in out,
            f"x == 0.6 is identically false for integer x; r=true must be UNSAT:\n{out}")
    out = run_model(
        """
        var 0..2: x :: output_var;
        var bool: r :: output_var;
        constraint int_ne_reif(x, 0.6, r);
        solve satisfy;
        """
    )
    require("r = true;" in out,
            f"x != 0.6 is identically true for integer x:\n{out}")

    # Plain int_ne with a fractional bound must not exclude valid integers.
    out = run_model(
        """
        var 0..2: x :: output_var;
        constraint int_ne(x, 0.6);
        constraint int_eq(x, 0);
        solve satisfy;
        """
    )
    require("x = 0;" in out, f"int_ne(x,0.6) must allow x=0:\n{out}")

    # variable on the right-hand side: 0.6 <= x  <=>  x >= 1 for integer x
    rels2 = {
        "int_le_reif": lambda a, b: a <= b,
        "int_lt_reif": lambda a, b: a < b,
        "int_ge_reif": lambda a, b: a >= b,
        "int_gt_reif": lambda a, b: a > b,
    }
    for xval in (0, 1):
        for name, fn in rels2.items():
            expect_r = "true" if fn(0.6, xval) else "false"
            out = run_model(
                f"""
                var 0..1: x :: output_var;
                var bool: r :: output_var;
                constraint {name}(0.6, x, r);
                constraint int_eq(x, {xval});
                solve satisfy;
                """
            )
            require(f"r = {expect_r};" in out,
                    f"{name}(0.6, x={xval}): expected r = {expect_r}:\n{out}")

    # Plain strict relations with fractional bounds must use the exact
    # lattice neighbor (ceil/floor of rhs), not rhs-1 / rhs+1.
    out = run_model(
        """
        var 0..2: x :: output_var;
        constraint int_lin_lt([1], [x], 0.6);
        solve maximize x;
        """
    )
    require("x = 0;" in out, f"int_lin_lt(x,0.6): max x must be 0 (was fabricated UNSAT):\n{out}")
    out = run_model(
        """
        var 0..2: x :: output_var;
        constraint int_lt(x, 0.6);
        solve maximize x;
        """
    )
    require("x = 0;" in out, f"int_lt(x,0.6): max x must be 0:\n{out}")
    out = run_model(
        """
        var 0..3: x :: output_var;
        constraint int_gt(x, 0.6);
        solve minimize x;
        """
    )
    require("x = 1;" in out, f"int_gt(x,0.6): min x must be 1:\n{out}")
    out = run_model(
        """
        var 0..5: x :: output_var;
        constraint int_lin_gt([1], [x], 2.5);
        solve minimize x;
        """
    )
    require("x = 3;" in out, f"int_lin_gt(x,2.5): min x must be 3:\n{out}")
    out = run_model(
        """
        var 0..5: x :: output_var;
        constraint int_lin_lt([1], [x], 2.5);
        solve maximize x;
        """
    )
    require("x = 2;" in out, f"int_lin_lt(x,2.5): max x must be 2:\n{out}")

    # Half-reification with fractional bound: r=true implies x < 0.6 (x<=0).
    out = run_model(
        """
        var 0..2: x :: output_var;
        var bool: r :: output_var;
        constraint int_lt_imp(x, 0.6, r);
        constraint bool_eq(r, true);
        solve maximize x;
        """
    )
    require("x = 0;" in out, f"int_lt_imp(x,0.6,true) must cap x at 0:\n{out}")

    # Non-integral lattice (0.6*x) in a reified relation: no exact unit step
    # exists -> honest UNKNOWN, never a rounded answer.
    out = run_model(
        """
        var 0..2: x :: output_var;
        var bool: r :: output_var;
        constraint int_lin_eq_reif([0.6], [x], 1, r);
        solve satisfy;
        """
    )
    require("=====UNKNOWN=====" in out,
            f"non-integral reif lattice must decline honestly:\n{out}")


def test_cp_gecode_offset() -> None:
    """gecode_*_element offset handling (const offsets work; the offset guard
    was inverted -- it declined plain constants and accepted variable offsets
    by dropping the variable part)."""
    out = run_model(
        """
        var 1..3: i :: output_var;
        var 0..9: v :: output_var;
        constraint gecode_int_element(i, 1, [5, 6, 9], v);
        constraint int_eq(v, 6);
        solve satisfy;
        """
    )
    require("i = 2;" in out and "v = 6;" in out,
            f"gecode_int_element with const offset failed:\n{out}")
    # variable offset is ill-formed: must not produce a wrong answer
    out = run_model(
        """
        var 2..2: y;
        var 2..3: i :: output_var;
        var 0..9: v :: output_var;
        constraint gecode_int_element(i, y, [5, 6], v);
        solve satisfy;
        """
    )
    # honest outcomes only: UNKNOWN (declined) or a correct SAT witness
    if "=====UNSATISFIABLE=====" in out:
        raise AssertionError(f"variable-offset element fabricated UNSAT:\n{out}")
    if "=====UNKNOWN=====" not in out:
        m_i = re.search(r"i = (\d+);", out)
        m_v = re.search(r"v = (\d+);", out)
        require(m_i and m_v, f"variable-offset element malformed output:\n{out}")
        iv, vv = int(m_i.group(1)), int(m_v.group(1))
        require([5, 6][iv - 2] == vv,
                f"variable-offset element wrong witness i={iv} v={vv}:\n{out}")


def test_cp_variable_element_differential() -> None:
    """Randomized array_var_int_element instances vs brute force.

    Exercises the CP variable-entry propagator (res/entry intersection,
    support-based index restriction); SAT witnesses are validated and
    UNSAT/UNKNOWN verdicts are compared against exhaustive enumeration.
    """
    rng = random.Random(20260812)
    for trial in range(120):
        n = rng.randint(2, 4)
        # entry i: constant or a variable with a tiny domain
        decls = []
        entries = []
        var_domains = {}
        for i in range(n):
            if rng.random() < 0.5:
                entries.append(f"e{i}")
                lo, hi = sorted((rng.randint(-3, 3), rng.randint(-3, 3)))
                decls.append(f"var {lo}..{hi}: e{i} :: output_var;")
                var_domains[f"e{i}"] = list(range(lo, hi + 1))
            else:
                entries.append(str(rng.randint(-3, 3)))
        ilo, ihi = 1, n
        vlo, vhi = sorted((rng.randint(-3, 0), rng.randint(0, 3)))
        parts = decls + [
            f"array [1..{n}] of var int: arr = [{', '.join(entries)}];",
            f"var {ilo}..{ihi}: idx :: output_var;",
            f"var {vlo}..{vhi}: val :: output_var;",
            "constraint array_var_int_element(idx, arr, val);",
        ]
        # sometimes pin val or idx to create infeasible instances
        pin_val = rng.random() < 0.4
        if pin_val:
            parts.append(f"constraint int_eq(val, {rng.randint(vlo, vhi)});")
        parts.append("solve satisfy;")
        model = "\n".join(parts)

        # brute force
        names = list(var_domains)
        doms = [var_domains[k] for k in names]
        feasible = False
        for combo in itertools.product(*doms):
            env = dict(zip(names, combo))
            arrvals = [env[e] if e in env else int(e) for e in entries]
            for idx in range(1, n + 1):
                val = arrvals[idx - 1]
                if not (vlo <= val <= vhi):
                    continue
                if pin_val:
                    pinned = int(parts[-2].split("int_eq(val, ")[1].split(")")[0])
                    if val != pinned:
                        continue
                feasible = True
                break
            if feasible:
                break

        out = run_model(model)
        if feasible:
            require("=====UNSATISFIABLE=====" not in out,
                    f"feasible element instance reported UNSAT (trial {trial}):\n{model}\n{out}")
            require("=====UNKNOWN=====" not in out,
                    f"feasible element instance reported UNKNOWN (trial {trial}):\n{model}\n{out}")
            m_i = re.search(r"idx = (-?\d+);", out)
            m_v = re.search(r"val = (-?\d+);", out)
            require(m_i and m_v, f"missing witness (trial {trial}):\n{out}")
            iv, vv = int(m_i.group(1)), int(m_v.group(1))
            env = {}
            for e in entries:
                if e.startswith("e"):
                    mm = re.search(rf"{e} = (-?\d+);", out)
                    require(mm, f"entry var {e} missing from output (trial {trial}):\n{out}")
                    env[e] = int(mm.group(1))
            arrvals = [env[e] if e in env else int(e) for e in entries]
            require(1 <= iv <= n and arrvals[iv - 1] == vv,
                    f"element witness violates val=arr[idx] (trial {trial}): idx={iv} val={vv} arr={arrvals}\n{model}\n{out}")
        else:
            require("=====UNSATISFIABLE=====" in out,
                    f"infeasible element instance not proven UNSAT (trial {trial}):\n{model}\n{out}")


def test_cp_allsolutions_reif_clause() -> None:
    """-a enumeration through the CP engine with reif/bool_clause records:
    distinctness, exact counts vs brute force, and the completion marker."""
    out = run_model(
        """
        var 1..3: x :: output_var;
        var 1..3: y :: output_var;
        var bool: r :: output_var;
        constraint int_le_reif(x, y, r);
        solve satisfy;
        """,
        "-a",
    )
    blocks = [b for b in out.split("----------") if "x =" in b]
    seen = set()
    for b in blocks:
        xm = re.search(r"x = (-?\d+);", b)
        ym = re.search(r"y = (-?\d+);", b)
        rm = re.search(r"r = (true|false);", b)
        require(xm and ym and rm, f"malformed -a block:\n{b}")
        tup = (int(xm.group(1)), int(ym.group(1)), rm.group(1))
        expect = "true" if tup[0] <= tup[1] else "false"
        require(tup[2] == expect, f"-a reif wrong: x={tup[0]} y={tup[1]} r={tup[2]}:\n{out}")
        require(tup not in seen, f"-a duplicate solution {tup}")
        seen.add(tup)
    # r is fully determined by (x,y) through the reif, so the model has
    # exactly 9 solutions: the complete 3x3 grid, each with its forced r.
    require(len(seen) == 9, f"expected 9 distinct (x,y,r) tuples on the 3x3 lattice, got {len(seen)}:\n{out}")
    grid = {(t[0], t[1]) for t in seen}
    require(grid == {(x, y) for x in (1, 2, 3) for y in (1, 2, 3)},
            f"-a reif enumeration is not the complete grid: {sorted(grid)}")
    require("==========" in out, f"-a reif enumeration missing completion:\n{out}")

    out = run_model(
        """
        var bool: a :: output_var;
        var bool: b :: output_var;
        var bool: c :: output_var;
        constraint bool_clause([a], [b, c]);
        solve satisfy;
        """,
        "-a",
    )
    blocks = [b for b in out.split("----------") if "a =" in b]
    seen = set()
    for b in blocks:
        am = re.search(r"a = (true|false);", b)
        bm = re.search(r"b = (true|false);", b)
        cm = re.search(r"c = (true|false);", b)
        require(am and bm and cm, f"malformed clause block:\n{b}")
        tup = (am.group(1), bm.group(1), cm.group(1))
        ok = (tup[0] == "true") or (tup[1] == "false") or (tup[2] == "false")
        require(ok, f"clause violated in -a solution {tup}:\n{out}")
        require(tup not in seen, f"duplicate clause solution {tup}")
        seen.add(tup)
    require(len(seen) == 7, f"bool_clause([a],[b,c]) has exactly 7 satisfying tuples, got {len(seen)}:\n{out}")
    require("==========" in out, f"-a clause enumeration missing completion:\n{out}")


def main() -> int:
    if not SOLVER.exists():
        print(f"missing solver binary: {SOLVER}", file=sys.stderr)
        return 2
    try:
        test_integer_strict()
        test_array_extrema()
        test_integer_reification_truth_table()
        test_float_linear_subset()
        test_table_and_constant_aliases()
        test_circuit()
        test_set_membership_constants()
        test_constant_arguments()
        test_declaration_indices_and_honest_unknown()
        test_all_solutions_and_extended_constraints()
        test_cp_minmax_constant_operands()
        test_fractional_lattice_relations()
        test_cp_gecode_offset()
        test_cp_variable_element_differential()
        test_cp_allsolutions_reif_clause()
    except AssertionError as exc:
        print(f"FlatZinc semantics test FAILED: {exc}", file=sys.stderr)
        return 1
    print("FlatZinc semantics: strict/reified int + extrema + float + table/circuit subset PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
