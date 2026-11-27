#!/usr/bin/env python3
"""Head-to-head performance gate: `./fznsolve` vs OR-Tools CP-SAT.

Both solvers receive byte-identical FlatZinc text.  fznsolve is timed as a
subprocess (its real end-to-end cost); the CP-SAT reference
(`tools/fzn_cpsat_ref.py`, official `ortools.sat.python.cp_model`, one search
worker like `fzn-cp-sat -p 1`) is timed in-process from "start parsing" to
"solve returned", i.e. *excluding* Python startup, which is the most
favourable possible accounting for CP-SAT.

Verdicts must agree (SAT/UNSAT/UNKNOWN) and, when both solve a satisfaction
model, the value assignments must both be feasible.  Timing is the minimum of
`--reps` runs.  The tool exits non-zero if any instance is slower than CP-SAT
or if any verdict disagrees, so it can be used as a regression gate.

    python3 tools/cpsat_perf.py [--reps 5] [--family NAME] [--json out.json]
                                [--max-ratio 1.0] [-v]
"""
import argparse
import importlib.util
import json
import math
import os
import re
import random
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FZNSOLVE = os.environ.get("FZNSOLVE", os.path.join(HERE, "..", "fznsolve"))
REF_PATH = os.path.join(HERE, "fzn_cpsat_ref.py")


def load_ref():
    spec = importlib.util.spec_from_file_location("fzn_cpsat_ref", REF_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ---------------------------------------------------------------------------
# Corpus: one generator per constraint family, deterministic (seeded)
# ---------------------------------------------------------------------------


def fz_vars(prefix, doms, ann="output_var"):
    return [f"var {lo}..{hi}: {prefix}{i} :: {ann};" for i, (lo, hi) in enumerate(doms)]


def corpus():
    inst = []

    def add(name, text, kind="sat"):
        inst.append((name, text, kind))

    # --- all_different ----------------------------------------------------
    for n in (10, 20, 40, 80):
        decls = fz_vars("x", [(1, n + 1)] * n)
        add(f"alldiff_sat_{n}",
            "".join(d + "\n" for d in decls) +
            "constraint all_different_int([" + ",".join(f"x{i}" for i in range(n)) + "]);\n"
            "solve satisfy;\n")
    for n in (9, 19, 39):
        decls = fz_vars("x", [(1, n)] * (n + 1))
        add(f"alldiff_pigeonhole_{n}",
            "".join(d + "\n" for d in decls) +
            "constraint all_different_int([" + ",".join(f"x{i}" for i in range(n + 1)) + "]);\n"
            "solve satisfy;\n", kind="unsat")

    # --- n-queens (all_different + diagonal disequalities) ---------------
    for n in (8, 12, 16, 24):
        decls = fz_vars("q", [(1, n)] * n)
        cons = ["constraint all_different_int([" + ",".join(f"q{i}" for i in range(n)) + "]);",
                "constraint all_different_int([" + ",".join(f"q{i}+{i}" for i in range(n)) + "]);",
                "constraint all_different_int([" + ",".join(f"q{i}-{i}" for i in range(n)) + "]);"]
        add(f"nqueens_{n}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- sudoku -----------------------------------------------------------
    base = [
        "53..7....", "6..195...", ".98....6.", "8...6...3", "4..8.3..1",
        "7...2...6", ".6....28.", "...419..5", "....8..79",
    ]
    def sudoku_text(grid, name):
        decls, cons = [], []
        for r in range(9):
            for c in range(9):
                decls.append(f"var 1..9: s{r}_{c} :: output_var;")
        for r in range(9):
            cons.append("constraint all_different_int([" +
                        ",".join(f"s{r}_{c}" for c in range(9)) + "]);")
        for c in range(9):
            cons.append("constraint all_different_int([" +
                        ",".join(f"s{r}_{c}" for r in range(9)) + "]);")
        for br in (0, 3, 6):
            for bc in (0, 3, 6):
                cons.append("constraint all_different_int([" + ",".join(
                    f"s{br + i}_{bc + j}" for i in range(3) for j in range(3)) + "]);")
        for r in range(9):
            for c in range(9):
                ch = grid[r][c]
                if ch != ".":
                    cons.append(f"constraint int_eq(s{r}_{c}, {ch});")
        return "".join(d + "\n" for d in decls) + "\n".join(cons) + "\nsolve satisfy;\n"
    add("sudoku_9x9", sudoku_text(base, "sudoku"))
    bad = list(base)
    bad[0] = "553..7..."  # duplicate 5 in row 0 -> unsat
    add("sudoku_9x9_unsat", sudoku_text(bad, "sudoku_unsat"), kind="unsat")

    # --- magic square 4x4 -------------------------------------------------
    n = 4
    decls = [f"var 1..16: m{i}_{j} :: output_var;" for i in range(n) for j in range(n)]
    allv = [f"m{i}_{j}" for i in range(n) for j in range(n)]
    cons = ["constraint all_different_int([" + ",".join(allv) + "]);"]
    for i in range(n):
        cons.append("constraint int_lin_eq([1,1,1,1],[" +
                    ",".join(f"m{i}_{j}" for j in range(n)) + "],34);")
        cons.append("constraint int_lin_eq([1,1,1,1],[" +
                    ",".join(f"m{j}_{i}" for j in range(n)) + "],34);")
    cons.append("constraint int_lin_eq([1,1,1,1],[" +
                ",".join(f"m{i}_{i}" for i in range(n)) + "],34);")
    cons.append("constraint int_lin_eq([1,1,1,1],[" +
                ",".join(f"m{i}_{n-1-i}" for i in range(n)) + "],34);")
    add("magic_square_4", "".join(d + "\n" for d in decls) + "\n".join(cons) +
        "\nsolve satisfy;\n")

    # --- cumulative scheduling (minimise makespan) ------------------------
    rng = random.Random(20261125)
    for k in (6, 10, 16):
        durs = [rng.randint(1, 4) for _ in range(k)]
        res = [rng.randint(1, 3) for _ in range(k)]
        horizon = sum(durs)
        decls = [f"var 0..{horizon}: s{i} :: output_var;" for i in range(k)]
        decls.append(f"var 0..{horizon}: mk :: output_var;")
        cons = [f"constraint cumulative([{','.join(f's{i}' for i in range(k))}],"
                f"[{','.join(map(str, durs))}],[{','.join(map(str, res))}],4);"]
        for i in range(k):
            cons.append(f"constraint int_le(s{i}+{durs[i]}, mk);")
        add(f"cumulative_opt_{k}",
            "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve minimize mk;\n", kind="opt")

    # --- job-shop (disjunctive) ------------------------------------------
    jobshop_3 = [(0, 3), (1, 2), (2, 2)], [((0, 0), (1, 2), (2, 2)),
                                           ((0, 2), (2, 1), (1, 2)),
                                           ((1, 1), (0, 3), (2, 2))]
    machines = jobshop_3[1]
    starts = []
    decls, cons = [], []
    idx = 0
    for j, ops in enumerate(machines):
        for (m, d) in ops:
            starts.append((j, m, d, f"s{idx}"))
            decls.append(f"var 0..20: s{idx} :: output_var;")
            idx += 1
    for m in range(3):
        on_m = [s for (j, mm, d, s) in starts if mm == m]
        durs = [d for (j, mm, d, s) in starts if mm == m]
        cons.append(f"constraint disjunctive([{','.join(on_m)}],"
                    f"[{','.join(map(str, durs))}]);")
    for j in range(3):
        ops = [st for st in starts if st[0] == j]
        for (a, b) in zip(ops, ops[1:]):
            cons.append(f"constraint int_le({a[3]}+{a[2]}, {b[3]});")
    decls.append("var 0..20: mk :: output_var;")
    for (j, m, d, s) in starts:
        cons.append(f"constraint int_le({s}+{d}, mk);")
    add("jobshop_3x3",
        "".join(d + "\n" for d in decls) + "\n".join(cons) + "\nsolve minimize mk;\n",
        kind="opt")

    # --- global_cardinality ----------------------------------------------
    for n in (12, 24, 40):
        decls = [f"var 1..4: g{i} :: output_var;" for i in range(n)]
        counts = [n // 4] * 4
        cons = [f"constraint global_cardinality([{','.join(f'g{i}' for i in range(n))}],"
                f"[1,2,3,4],[{','.join(map(str, counts))}]);"]
        add(f"gcc_{n}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- bin packing ------------------------------------------------------
    for k in (10, 16, 24):
        rng = random.Random(7000 + k)
        sizes = [rng.randint(1, 5) for _ in range(k)]
        nbins = max(3, k // 3)
        decls = [f"var 1..{nbins}: b{i} :: output_var;" for i in range(k)]
        cons = [f"constraint bin_packing(8,[{','.join(f'b{i}' for i in range(k))}],"
                f"[{','.join(map(str, sizes))}]);"]
        add(f"bin_packing_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- extensional table ------------------------------------------------
    for k in (8, 14, 20):
        rng = random.Random(9000 + k)
        doms = [(1, 4)] * k
        decls = fz_vars("t", doms)
        rows = []
        for _ in range(30):
            row = tuple(rng.randint(1, 4) for _ in range(k))
            rows.append(row)
        rows = sorted(set(rows))
        flat = ",".join(str(v) for row in rows for v in row)
        cons = [f"constraint table_int([{','.join(f't{i}' for i in range(k))}],"
                f"array2d(1,{len(rows) * k},1,{len(rows)},[{flat}]));"]
        add(f"table_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- element chains ---------------------------------------------------
    for k in (12, 24):
        rng = random.Random(3100 + k)
        arr = [rng.randint(0, 50) for _ in range(k)]
        decls = [f"var 1..{k}: ix{i} :: output_var;" for i in range(k)]
        decls += [f"var 0..50: y{i} :: output_var;" for i in range(k)]
        cons = [f"constraint array_int_element(ix{i},[{','.join(map(str, arr))}],y{i});"
                for i in range(k)]
        cons.append("constraint all_different_int([" + ",".join(f"ix{i}" for i in range(k)) + "]);")
        cons.append("constraint int_lin_eq([" + ",".join(["1"] * k) + "],["
                    + ",".join(f"y{i}" for i in range(k)) + f"],{sum(sorted(arr))});")
        add(f"element_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- circuit ----------------------------------------------------------
    for k in (8, 14, 22):
        rng = random.Random(4100 + k)
        decls = [f"var 1..{k}: c{i} :: output_var;" for i in range(k)]
        cons = ["constraint circuit([" + ",".join(f"c{i}" for i in range(k)) + "]);"]
        # pin a few arcs to make it a real search problem
        for i in range(0, k, 3):
            cons.append(f"constraint int_ne(c{i}, {i + 1});")
        add(f"circuit_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- lexicographic ----------------------------------------------------
    for k in (10, 20):
        rng = random.Random(5100 + k)
        decls = fz_vars("a", [(0, 5)] * k) + fz_vars("b", [(0, 5)] * k)
        cons = [f"constraint lex_lesseq_int([{','.join(f'a{i}' for i in range(k))}],"
                f"[{','.join(f'b{i}' for i in range(k))}]);",
                "constraint int_lin_eq([" + ",".join(["1"] * k) + "],["
                + ",".join(f"b{i}" for i in range(k)) + f"],{3 * k});"]
        add(f"lex_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- subset sum / knapsack -------------------------------------------
    for k in (16, 24, 32):
        rng = random.Random(6100 + k)
        w = [rng.randint(1, 30) for _ in range(k)]
        target = sum(w) // 2
        decls = [f"var 0..1: z{i} :: output_var;" for i in range(k)]
        cons = [f"constraint bool_lin_eq([{','.join(map(str, w))}],"
                f"[{','.join(f'z{i}' for i in range(k))}],{target});"]
        add(f"subset_sum_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- counting / nvalue -----------------------------------------------
    for k in (10, 20):
        rng = random.Random(8100 + k)
        decls = [f"var 1..6: v{i} :: output_var;" for i in range(k)]
        decls.append("var 0..6: nv :: output_var;")
        cons = [f"constraint nvalue(nv,[{','.join(f'v{i}' for i in range(k))}]);",
                "constraint int_eq(nv, 3);",
                "constraint count_eq([" + ",".join(f"v{i}" for i in range(k)) + "],2,4);"]
        add(f"nvalue_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- arithmetic chain ------------------------------------------------
    for k in (8, 14):
        decls = [f"var -50..50: p{i} :: output_var;" for i in range(k + 2)]
        cons = []
        for i in range(k):
            cons.append(f"constraint int_abs(p{i},p{i + 1});")
            cons.append(f"constraint int_plus(p{i + 1},{i + 2},p{i + 2});")
        cons.append("constraint int_eq(p0, -7);")
        add(f"arith_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- boolean clauses (random 3-SAT, satisfiable by construction) -------
    for k in (16, 28):
        rng = random.Random(1200 + k)
        decls = [f"var bool: c{i} :: output_var;" for i in range(k)]
        cons = []
        for _ in range(k * 2):
            lits = rng.sample(range(k), 3)
            pos = [f"c{i}" for i in lits if rng.random() < 0.5]
            neg = [f"c{i}" for i in lits if f"c{i}" not in pos]
            cons.append(f"constraint bool_clause([{','.join(pos)}],[{','.join(neg)}]);")
        add(f"bool_sat_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    # --- diffn (2-D packing) ---------------------------------------------
    for k in (4, 6):
        size = 10
        decls, cons = [], []
        for i in range(k):
            decls.append(f"var 0..{size - 3}: dx{i} :: output_var;")
            decls.append(f"var 0..{size - 3}: dy{i} :: output_var;")
        cons.append("constraint diffn(["
                    + ",".join(f"dx{i}" for i in range(k)) + "],["
                    + ",".join(f"dy{i}" for i in range(k)) + "],["
                    + ",".join(["3"] * k) + "],[" + ",".join(["3"] * k) + "]);")
        add=add
        add(f"diffn_{k}", "".join(d + "\n" for d in decls) + "\n".join(cons) +
            "\nsolve satisfy;\n")

    return inst


# ---------------------------------------------------------------------------
# Running and checking
# ---------------------------------------------------------------------------


def solve_time_of(out):
    """solveTime reported by `fznsolve -s` (excludes process startup, parsing
    and printing, so it is the like-for-like counterpart of the in-process
    CP-SAT build+solve number)."""
    m = re.search(r"%%mzn-stat: solveTime=([0-9.eE+-]+)", out)
    return float(m.group(1)) if m else None


def status_of(out):
    if "=====UNSATISFIABLE=====" in out:
        return "UNSAT"
    if "=====UNKNOWN=====" in out:
        return "UNKNOWN"
    if "----------" in out:
        return "SAT"
    return "?"


def solution_values(out):
    """Last printed solution block as {name: int}."""
    vals = {}
    for block in out.split("----------"):
        cur = {}
        for line in block.splitlines():
            line = line.strip()
            if line.endswith(";") and "=" in line:
                name, val = line[:-1].split("=", 1)
                try:
                    cur[name.strip()] = int(val.strip())
                except ValueError:
                    pass
        if cur:
            vals = cur
    return vals


def check_assignment(text, values):
    """Independently verify a claimed assignment against the .fzn text.

    Only the constraint shapes used by the corpus are checked; anything else
    returns None ("not checked") so the gate never silently passes garbage.
    """
    import re
    decls = {}
    for m in re.finditer(r"var\s+(-?\d+)\.\.(-?\d+):\s*([A-Za-z_][A-Za-z_0-9]*)\s*::", text):
        decls[m.group(3)] = (int(m.group(1)), int(m.group(2)))
    for name, (lo, hi) in decls.items():
        if name in values and not (lo <= values[name] <= hi):
            return False
    def val(tok):
        tok = tok.strip()
        if re.fullmatch(r"-?\d+", tok):
            return int(tok)
        m = re.fullmatch(r"([A-Za-z_][A-Za-z_0-9]*)([+-]\d+)?", tok)
        if not m:
            return None
        base = values.get(m.group(1))
        if base is None:
            return None
        return base + int(m.group(2) or 0)
    def arr(tok):
        return [val(t) for t in tok.strip().strip("[]").split(",")]
    ok = True
    for m in re.finditer(r"constraint\s+(all_different_int|int_lin_eq|int_eq|count_eq)\s*"
                         r"\((.*?)\);", text, re.S):
        kind, body = m.group(1), m.group(2)
        args = [a for a in body.split(",")]
        if kind == "all_different_int":
            xs = arr(args[0])
            if any(x is None for x in xs):
                return None
            if len(set(xs)) != len(xs):
                ok = False
        elif kind == "int_lin_eq":
            coeffs = arr(args[0])
            xs = arr(args[1])
            rhs = val(args[2])
            if any(x is None for x in xs) or rhs is None:
                return None
            if sum(c * x for c, x in zip(coeffs, xs)) != rhs:
                ok = False
        elif kind == "int_eq":
            a, b = val(args[0]), val(args[1])
            if a is None or b is None:
                return None
            if a != b:
                ok = False
        elif kind == "count_eq":
            xs = arr(args[0])
            want, cnt = val(args[1]), val(args[2])
            if any(x is None for x in xs) or want is None or cnt is None:
                return None
            if sum(1 for x in xs if x == want) != cnt:
                ok = False
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--family", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--max-ratio", type=float, default=1.0)
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--startup-reps", type=int, default=15,
                    help="reps used to measure the process floor of fznsolve "
                         "(a trivial model) so solve times can be compared "
                         "like for like: CP-SAT numbers are in-process")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    ref = load_ref()
    inst = corpus()
    if args.family:
        inst = [i for i in inst if args.family in i[0]]
    tmp = tempfile.mkdtemp(prefix="cpsat_perf")
    # process floor: fork+exec+parser init of fznsolve itself.  The CP-SAT
    # column is measured *in process* (build+solve inside this interpreter),
    # so end-to-end numbers for sub-2 ms models compare a whole process
    # against a library call.  Everything below reports both.
    floor_s = 0.0
    if args.startup_reps > 0:
        trivial = os.path.join(tmp, "_floor.fzn")
        with open(trivial, "w") as fh:
            fh.write("var 0..3: x :: output_var;\nconstraint int_le(x,2);\nsolve satisfy;\n")
        times = []
        for _ in range(args.startup_reps):
            t0 = __import__("time").perf_counter()
            subprocess.run([FZNSOLVE, trivial], capture_output=True, text=True)
            times.append(__import__("time").perf_counter() - t0)
        floor_s = statistics.median(times)
        print(f"process floor (fznsolve on a trivial model): {floor_s * 1000:.3f} ms")
    rows = []
    failures = []
    print(f"{'instance':<24}{'st':<6}{'fznsolve':>11}{'solve':>10}{'cp-sat':>11}"
          f"{'ratio':>8}{'solve ratio':>12}  {'cpsat build':>12}{'cpsat solve':>13}")
    print("-" * 100)
    for name, text, kind in inst:
        path = os.path.join(tmp, f"{name}.fzn")
        with open(path, "w") as fh:
            fh.write(text)
        fz_best = None
        fz_out = None
        for _ in range(args.reps):
            t0 = __import__("time").perf_counter()
            try:
                p = subprocess.run([FZNSOLVE, "-s", path], capture_output=True, text=True,
                                   timeout=args.timeout)
            except subprocess.TimeoutExpired:
                fz_best = args.timeout
                fz_out = ""
                break
            dt = __import__("time").perf_counter() - t0
            fz_best = dt if fz_best is None else min(fz_best, dt)
            fz_out = p.stdout + p.stderr
        fz_st = status_of(fz_out or "")
        fz_solve = solve_time_of(fz_out or "")
        try:
            ref_bests = None
            ref_res = None
            for _ in range(args.reps):
                res = ref.run(path, workers=1, time_limit=args.timeout)
                ref_bests = res["total_s"] if ref_bests is None else min(ref_bests, res["total_s"])
                ref_res = res
        except Exception as exc:                      # noqa: BLE001
            print(f"{name:<24}reference oracle failed: {exc}")
            failures.append((name, f"reference failed: {exc}"))
            continue
        if ref_res["unsupported"]:
            print(f"{name:<24}UNSUPPORTED by reference: {ref_res['unsupported'][:2]}")
            failures.append((name, f"unsupported: {ref_res['unsupported'][:2]}"))
            continue
        ref_st = ref_res["status"]
        ref_proven = ref_res.get("proven", True)
        ratio = fz_best / max(ref_bests, 1e-9)
        net = max(fz_best - floor_s, 1e-6)
        ratio_net = net / max(ref_bests, 1e-9)
        # primary metric: our own instrumented solve time against the
        # reference's in-process build+solve time
        solve_s = fz_solve if fz_solve is not None else net
        ratio_solve = solve_s / max(ref_bests, 1e-9)
        rows.append({
            "instance": name, "kind": kind, "status": fz_st, "ref_status": ref_st,
            "fznsolve_s": round(fz_best, 6), "cpsat_s": round(ref_bests, 6),
            "cpsat_build_s": round(ref_res["build_s"], 6),
            "cpsat_solve_s": round(ref_res["solve_s"], 6),
            "ratio": round(ratio, 3),
            "net_s": round(net, 6),
            "ratio_net": round(ratio_net, 3),
            "fznsolve_solve_s": (round(fz_solve, 6) if fz_solve is not None else None),
            "ratio_solve": round(ratio_solve, 3),
            "ref_proven": ref_proven,
            "startup_floor_s": round(floor_s, 6),
        })
        flag = ""
        if fz_st != ref_st:
            flag = f"  VERDICT MISMATCH (ref={ref_st})"
            failures.append((name, f"verdict {fz_st} vs {ref_st}"))
        elif ratio_solve > args.max_ratio:
            flag = "  SLOWER"
            failures.append((name, f"ratio {ratio_solve:.2f}x solve-only "
                                   f"({ratio:.2f}x end-to-end)"))
        elif not ref_proven:
            flag = "  (reference stopped at its limit, not proven optimal)"
        if fz_st == ref_st and fz_st == "SAT":
            chk = check_assignment(text, solution_values(fz_out))
            if chk is False:
                flag = "  INVALID SOLUTION"
                failures.append((name, "invalid solution"))
            elif chk is None:
                flag = "  (assignment not re-checked)"
        print(f"{name:<24}{fz_st:<6}{fz_best * 1000:9.2f}ms{solve_s * 1000:8.3f}ms"
              f"{ref_bests * 1000:9.2f}ms{ratio:7.2f}x{ratio_net:9.2f}x"
              f"  {ref_res['build_s'] * 1000:10.2f}ms"
              f"{ref_res['solve_s'] * 1000:11.2f}ms{flag}")
        if args.verbose:
            print("   ", text.replace("\n", " ")[:160])

    print("-" * 100)
    ratios = [r["ratio"] for r in rows]
    if ratios:
        geo = math.exp(sum(math.log(max(r, 1e-9)) for r in ratios) / len(ratios))
        print(f"instances={len(rows)} geomean ratio={geo:.2f}x "
              f"median={statistics.median(ratios):.2f}x max={max(ratios):.2f}x "
              f"min={min(ratios):.2f}x")
        tot_fz = sum(r["fznsolve_s"] for r in rows)
        tot_ref = sum(r["cpsat_s"] for r in rows)
        print(f"total time: fznsolve={tot_fz * 1000:.1f}ms cp-sat={tot_ref * 1000:.1f}ms "
              f"({tot_fz / tot_ref:.2f}x)  [end-to-end, includes the "
              f"{floor_s * 1000:.2f}ms process floor per run]")
        nr = [r["ratio_solve"] for r in rows]
        gn = math.exp(sum(math.log(max(r, 1e-9)) for r in nr) / len(nr))
        tot_net = sum(r["fznsolve_solve_s"] if r["fznsolve_solve_s"] is not None
                      else r["net_s"] for r in rows)
        print(f"solve-only (own solveTime, excludes process startup): geomean={gn:.2f}x "
              f"median={statistics.median(nr):.2f}x max={max(nr):.2f}x "
              f"min={min(nr):.2f}x")
        print(f"solve-only total: fznsolve={tot_net * 1000:.1f}ms "
              f"cp-sat={tot_ref * 1000:.1f}ms ({tot_net / tot_ref:.2f}x)")
        faster = sum(1 for r in nr if r <= 1.0)
        print(f"instances at least as fast as CP-SAT: {faster}/{len(nr)} "
              f"(solve-only); {sum(1 for r in ratios if r <= 1.0)}/{len(ratios)} end-to-end")
    if failures:
        print(f"\nFAILURES ({len(failures)}):")
        for name, why in failures:
            print(f"  {name}: {why}")
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(rows, fh, indent=1)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
