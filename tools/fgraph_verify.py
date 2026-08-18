"""Verification for the psolve functional-graph constraint family
(docs/FUNCTIONAL_GRAPH.md):

    orbit_len(nxt, s, l)         l = #distinct states before first repeat
    orbit_transient(nxt, s, t)   t = steps from s to the first on-cycle state
    orbit_cycle_len(nxt, s, c)   c = length of the cycle s's walk enters
    orbit_on_cycle(nxt, s)       s lies on a cycle
    orbit_len_capped(nxt, k, s, l)  l = min(orbit(nxt,s), k)  (presolve target)
    array_bool_and(bs, o)        o <-> AND(bs)

Every fact is cross-checked against an independent pure-Python functional
-graph brute force, sharing the oracle with tools/procstates_orbit_verify.py's
invariant l = t + c, which is also exercised as a model-level constraint.

Modes (argv: N [seed]):
  * hand-computed pins on the 6-state graph [1,2,3,1,5,3] (0-based state ids,
    values in [0..n) as the predicate requires):
        0->1->2->3->1 (cycle {1,2,3}), 4->5->3
      hence tr=(1,0,0,0,2,1), cl=(3,3,3,3,3,3), orb=(4,3,3,3,5,4)
  * the real 65536-state procstates instance (spot states against the table)
  * randomized functional digraphs n=3..18 vs brute force:
      satisfy/-a full (s,fact) projection sets, maximization optima,
      capped optima, len-fixed filtering, UNSAT cross-checks
  * honest-decline pins: out-of-range table entry, cap>n, oversized table
  * array_bool_and unit pins + fuzz against python and-semantics

Honesty: a ``WRONG`` line is printed per violation; summary
``fgraph_verify: pins_bad=X real_bad=Y fuzz_N=... -> WRONG=Z (seed=..)``;
exit 0 iff Z==0 and pins_bad==0 and real_bad==0.

env: PSOLVE_FZNSOLVE overrides the binary.
"""
import os
import random
import re
import subprocess
import sys

FZNSOLVE = os.path.abspath(os.environ.get("PSOLVE_FZNSOLVE", os.path.join(os.path.dirname(__file__), "..", "fznsolve")))

WRONG = 0
def bad(*a):
    global WRONG
    WRONG += 1
    print("  WRONG:", *a)

# ---------------------------------------------------------------- oracle ---
def fg_oracle(nxt):
    """per-state (transient, cycle_len, orbit) for functional graph nxt."""
    n = len(nxt)
    tr = [None] * n
    for s in range(n):
        if tr[s] is not None:
            continue
        order = []
        pos = {}
        cur = s
        while tr[cur] is None and cur not in pos:
            pos[cur] = len(order)
            order.append(cur)
            cur = nxt[cur]
        if tr[cur] is None:
            cyc = len(order) - pos[cur]
            for i in range(pos[cur], len(order)):
                tr[order[i]] = (0, cyc)
            for i in range(pos[cur] - 1, -1, -1):
                tr[order[i]] = (tr[order[i + 1]][0] + 1, tr[order[i + 1]][1])
        else:
            t0, c0 = tr[cur]
            for i in range(len(order) - 1, -1, -1):
                tr[order[i]] = (t0 + (len(order) - i), c0)
    out = []
    for s in range(n):
        t, c = tr[s]
        out.append((t, c, t + c))
    return out

def run_fzn(name, text, flags=(), allow_unknown=False):
    p = f"/tmp/fgraph_{name}.fzn"
    with open(p, "w") as f:
        f.write(text)
    r = subprocess.run([FZNSOLVE] + list(flags) + [p], capture_output=True, text=True, timeout=120)
    out = r.stdout
    if not allow_unknown and "UNKNOWN" in out:
        bad(name, "unexpected UNKNOWN", out.splitlines()[:2], r.stderr[:100])
    return out

def parse_sols(out):
    sols = []
    cur = {}
    for line in out.splitlines():
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*) = (-?\d+);$", line.strip())
        if m:
            cur[m.group(1)] = int(m.group(2))
        elif line.strip() == "----------":
            sols.append(cur)
            cur = {}
    return sols

def model(nxt, decls, cons, solve):
    n = len(nxt)
    lines = ["array [1..%d] of int: nxt = [%s];" % (n, ",".join(map(str, nxt)))]
    lines += decls
    lines += ["constraint " + cx + ";" for cx in cons]
    lines.append("solve %s;" % solve)
    return "\n".join(lines) + "\n"

def table_str(nxt):
    return "[%s]" % ",".join(map(str, nxt))

# ------------------------------------------------------------------ pins ---
PIN_TABLE = [1, 2, 3, 1, 5, 3]
PIN_TR = (1, 0, 0, 0, 2, 1)
PIN_CL = (3, 3, 3, 3, 3, 3)
PIN_ORB = (4, 3, 3, 3, 5, 4)

def pins():
    badp = 0
    def expect(name, text, check, flags=(), allow_unknown=False):
        nonlocal badp
        out = run_fzn(name, text, flags, allow_unknown=allow_unknown)
        sols = parse_sols(out)
        if not check(out, sols):
            badp += 1
            bad("pin", name, "failed:", out.splitlines()[:6])
    # s=4 fixed: tr=2, cl=3, orb=5, capped cap=3 -> 3, identity l=t+c
    def c_fixed(out, sols):
        return len(sols) == 1 and sols[0].get("t") == 2 and sols[0].get("c") == 3 and sols[0].get("l") == 5
    expect("pin_facts", model(PIN_TABLE,
        ["var 0..6: t :: output_var;", "var 1..6: c :: output_var;", "var 1..6: l :: output_var;"],
        ["orbit_transient(nxt, 4, t)", "orbit_cycle_len(nxt, 4, c)", "orbit_len(nxt, 4, l)",
         "int_lin_eq([1,-1,-1],[l,t,c],0)"], "satisfy"), c_fixed)
    # on_cycle full projection: exactly {1,2,3}
    def c_onc(out, sols):
        return sorted(x["s"] for x in sols) == [1, 2, 3]
    expect("pin_oncycle", model(PIN_TABLE, ["var 0..5: s :: output_var;"],
        ["orbit_on_cycle(nxt, s)"], "satisfy"), c_onc, flags=("-a",))
    # capped maximize: max orbit 5, cap 3 -> optimum 3 with objectiveBound
    def c_cap(out, sols):
        return len(sols) >= 1 and sols[-1].get("l") == 3 and "objective=3" in out
    expect("pin_cap", model(PIN_TABLE,
        ["var 0..5: s :: output_var;", "var 1..6: l :: output_var;"],
        ["orbit_len_capped(nxt, 3, s, l)"], "maximize l"), c_cap)
    # cap >= n: capped == uncapped (optimum 5)
    def c_cap6(out, sols):
        return len(sols) >= 1 and sols[-1].get("l") == 5 and "objective=5" in out and sols[-1].get("s") == 4
    expect("pin_capn", model(PIN_TABLE,
        ["var 0..5: s :: output_var;", "var 1..6: l :: output_var;"],
        ["orbit_len_capped(nxt, 6, s, l)"], "maximize l"), c_cap6)
    # UNSAT: transient of state 4 is 2, not 1
    def c_unsat(out, sols):
        return "UNSATISFIABLE" in out
    expect("pin_unsat", model(PIN_TABLE, [],
        ["orbit_transient(nxt, 4, 1)"], "satisfy"), c_unsat)
    # honest declines
    def c_decl(out, sols):
        return "UNKNOWN" in out
    expect("pin_decl_range", model([1, 2, 3, 9], ["var 1..4: l :: output_var;"],
        ["orbit_len(nxt, 0, l)"], "satisfy"), c_decl, allow_unknown=True)
    expect("pin_decl_cap", model(PIN_TABLE, ["var 1..6: l :: output_var;"],
        ["orbit_len_capped(nxt, 7, 0, l)"], "satisfy"), c_decl, allow_unknown=True)
    # AND pins
    def c_and1(out, sols):
        return len(sols) == 1 and sols[0].get("b1") == 1 and sols[0].get("b2") == 1
    expect("pin_and1", "\n".join([
        "var 0..1: b1 :: output_var;", "var 0..1: b2 :: output_var;", "var 0..1: o :: output_var;",
        "constraint array_bool_and([b1,b2], o);", "constraint int_eq(o,1);", "solve satisfy;", ""]), c_and1)
    def c_and0(out, sols):
        pairs = sorted((x["b1"], x["b2"]) for x in sols)
        return pairs == [(0, 0), (0, 1), (1, 0)]
    expect("pin_and0", "\n".join([
        "var 0..1: b1 :: output_var;", "var 0..1: b2 :: output_var;", "var 0..1: o :: output_var;",
        "constraint array_bool_and([b1,b2], o);", "constraint int_eq(o,0);", "solve satisfy;", ""]), c_and0, flags=("-a",))
    def c_andu(out, sols):
        return "UNSATISFIABLE" in out
    expect("pin_and_unsat", "\n".join([
        "array [1..2] of int: bs = [true, false];",
        "var 0..1: o :: output_var;",
        "constraint array_bool_and([bs[1],bs[2]], o);",
        "constraint int_eq(o,1);", "solve satisfy;", ""]), c_andu)
    return badp

# ------------------------------------------------------------------ real ---
def real_instance():
    """spot-check the real 65536-state table against python oracle facts"""
    here = os.path.dirname(__file__)
    tab = []
    with open(os.path.join(here, "..", "examples", "procstates", "procstates_next.dzn")) as f:
        txt = f.read()
    m = re.search(r"\[(.*)\]", txt, re.S)
    tab = [int(x) for x in m.group(1).replace("\n", " ").split(",") if x.strip()]
    if len(tab) != 65536:
        bad("real", "table load failed", len(tab))
        return 1
    facts = fg_oracle(tab)
    spots = {51641: facts[51641], 15679: facts[15679], 0: facts[0], 65535: facts[65535]}
    badr = 0
    for s, (t, c, l) in spots.items():
        out = run_fzn(f"real_s{s}", model(tab,
            ["var 0..65536: t :: output_var;", "var 1..65536: c :: output_var;", "var 1..65536: l :: output_var;"],
            [f"orbit_transient(nxt, {s}, t)", f"orbit_cycle_len(nxt, {s}, c)", f"orbit_len(nxt, {s}, l)"],
            "satisfy"))
        sols = parse_sols(out)
        if len(sols) != 1 or sols[0].get("t") != t or sols[0].get("c") != c or sols[0].get("l") != l:
            badr += 1
            bad("real", s, "want", (t, c, l), "got", sols[:1])
    # global optimum through each facade objective
    out = run_fzn("real_max_orb", model(tab,
        ["var 0..65535: s :: output_var;", "var 1..65536: l :: output_var;"],
        ["orbit_len(nxt, s, l)"], "maximize l"))
    if not (parse_sols(out) and parse_sols(out)[-1].get("l") == facts_max(facts) and "objective=44" in out):
        badr += 1
        bad("real_max_orb", out.splitlines()[:6])
    out = run_fzn("real_max_tr", model(tab,
        ["var 0..65535: s :: output_var;", "var 0..65536: t :: output_var;"],
        ["orbit_transient(nxt, s, t)"], "maximize t"))
    mt = max(f[0] for f in facts)
    if not (parse_sols(out) and parse_sols(out)[-1].get("t") == mt and f"objective={mt}" in out):
        badr += 1
        bad("real_max_tr", "want", mt, out.splitlines()[:6])
    out = run_fzn("real_max_cl", model(tab,
        ["var 0..65535: s :: output_var;", "var 1..65536: c :: output_var;"],
        ["orbit_cycle_len(nxt, s, c)"], "maximize c"))
    mc = max(f[1] for f in facts)
    if not (parse_sols(out) and parse_sols(out)[-1].get("c") == mc and f"objective={mc}" in out):
        badr += 1
        bad("real_max_cl", "want", mc, out.splitlines()[:6])
    out = run_fzn("real_cap44", model(tab,
        ["var 0..65535: s :: output_var;", "var 1..64: l :: output_var;"],
        ["orbit_len_capped(nxt, 64, s, l)"], "maximize l"))
    if not (parse_sols(out) and parse_sols(out)[-1].get("l") == 44 and "objective=44" in out):
        badr += 1
        bad("real_cap44", out.splitlines()[:6])
    return badr

def facts_max(facts):
    return max(f[2] for f in facts)

# ------------------------------------------------------------------ fuzz ---
def fuzz(N, seed):
    rng = random.Random(seed)
    badf = 0
    for it in range(N):
        n = rng.randint(3, 18)
        nxt = [rng.randrange(n) for _ in range(n)]
        facts = fg_oracle(nxt)
        mode = it % 4
        name = f"f{it}"
        if mode == 0:
            # full (s, orbit) projection with -a
            out = run_fzn(name, model(nxt,
                ["var 0..%d: s :: output_var;" % (n - 1), "var 1..%d: l :: output_var;" % n],
                ["orbit_len(nxt, s, l)"], "satisfy"), flags=("-a",))
            got = sorted((x["s"], x["l"]) for x in parse_sols(out))
            want = sorted({(s, facts[s][2]) for s in range(n)})
            if got != want:
                badf += 1; bad(name, "orbit projection", nxt, got[:5], want[:5])
        elif mode == 1:
            # transient+len identity model, fix s -> exact pair
            s0 = rng.randrange(n)
            t0, c0, l0 = facts[s0]
            out = run_fzn(name, model(nxt,
                ["var 0..%d: t :: output_var;" % n, "var 1..%d: l :: output_var;" % n],
                [f"orbit_transient(nxt, {s0}, t)", f"orbit_len(nxt, {s0}, l)"], "satisfy"))
            sols = parse_sols(out)
            if len(sols) != 1 or sols[0].get("t") != t0 or sols[0].get("l") != l0:
                badf += 1; bad(name, "fixed facts", nxt, s0, (t0, l0), sols[:1])
            if l0 != t0 + c0:
                badf += 1; bad(name, "oracle identity broken")
        elif mode == 2:
            # capped maximize: optimum min(max_orbit, cap)
            cap = rng.randint(1, n + 2)
            if cap <= n:
                want = min(max(f[2] for f in facts), cap)
                out = run_fzn(name, model(nxt,
                    ["var 0..%d: s :: output_var;" % (n - 1), "var 1..%d: l :: output_var;" % n],
                    [f"orbit_len_capped(nxt, {cap}, s, l)"], "maximize l"))
                sols = parse_sols(out)
                if not sols or sols[-1].get("l") != want or f"objective={want}" not in out:
                    badf += 1; bad(name, "cap max", nxt, cap, want, out.splitlines()[:6])
        else:
            # cycle_len maximize + on_cycle restricted maximize == same cycle l
            mc = max(f[1] for f in facts)
            out = run_fzn(name, model(nxt,
                ["var 0..%d: s :: output_var;" % (n - 1), "var 1..%d: c :: output_var;" % n],
                ["orbit_cycle_len(nxt, s, c)", "orbit_on_cycle(nxt, s)"], "maximize c"))
            sols = parse_sols(out)
            if not sols or sols[-1].get("c") != mc or f"objective={mc}" not in out:
                badf += 1; bad(name, "cycle max", nxt, mc, out.splitlines()[:6])
    return badf

def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260818
    pb = pins()
    rb = real_instance()
    fb = fuzz(N, seed)
    print(f"fgraph_verify: pins_bad={pb} real_bad={rb} fuzz_N={N} -> WRONG={WRONG} (seed={seed})")
    return 0 if WRONG == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
