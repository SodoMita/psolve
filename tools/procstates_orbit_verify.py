#!/usr/bin/env python3
"""Verification for the psolve global constraint `orbit_len(next, start, len)`
(functional-graph orbit length: number of distinct states visited by the
walk start, next[start], ..., before its first repeated state).

Checks, in the project's calibration style:
  * pins with known answers (handcrafted functional digraphs: cycles,
    tails, restricted start domains, UNSAT, degenerate n=1, a decline pin
    with an out-of-range table that must NOT produce a fabricated answer);
  * the real 65536-state "procstates" automaton (Unesty/Doing): the solver
    must claim len = 44 as a PROVEN optimum; the tool independently
    computes every state's orbit from the transition data file and checks
    the printed start really attains 44 and that no state attains 45;
  * randomized toy automata (n = 3..20), satisfy(-a) full projection sets
    and maximize optima, against a pure-Python brute force.

Pre-change discrimination: a binary without the global prints
=====UNKNOWN===== on every model here -> every pin and every fuzzed case
fails (exit 1).

usage: procstates_orbit_verify.py [N] [seed]
env:   PSOLVE_FZNSOLVE overrides the binary; PSOLVE_PROC_NOCHECK walks files.
"""
import os, re, sys, subprocess, random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = "/tmp/procstates_orbit"
FZNSOLVE = os.path.abspath(os.environ.get("PSOLVE_FZNSOLVE",
                                         os.path.join(ROOT, "fznsolve")))
UNSAT, UNKNOWN = "=====UNSATISFIABLE=====", "=====UNKNOWN====="


def brute_orbits(nxt):
    n = len(nxt)
    depth = [0] * n
    for s in range(n):
        if depth[s]:
            continue
        order = []
        pos = {}
        cur = s
        while True:
            if depth[cur]:
                for i in range(len(order) - 1, -1, -1):
                    depth[order[i]] = depth[cur] + (len(order) - i)
                break
            if cur in pos:
                cyc = len(order) - pos[cur]
                for i in range(pos[cur], len(order)):
                    depth[order[i]] = cyc
                for i in range(pos[cur] - 1, -1, -1):
                    depth[order[i]] = depth[order[i + 1]] + 1
                break
            pos[cur] = len(order)
            order.append(cur)
            cur = nxt[cur]
    return depth


def fzn_model(nxt, solve, lo=0, hi=None, lenfix=None):
    n = len(nxt)
    if hi is None:
        hi = n - 1
    lines = []
    lines.append("array [1..%d] of int: nxt = [%s];" % (n, ",".join(map(str, nxt))))
    lines.append("var %d..%d: start :: output_var;" % (lo, hi))
    if lenfix is not None:
        lines.append("var %d..%d: len :: output_var;" % (lenfix, lenfix))
    else:
        lines.append("var 1..%d: len :: output_var;" % n)
    lines.append("constraint orbit_len(nxt, start, len);")
    lines.append("solve %s;" % solve)
    return "\n".join(lines) + "\n"


def run_fzn(name, text, flags=()):
    p = os.path.join(TMP, name + ".fzn")
    open(p, "w").write(text)
    r = subprocess.run([FZNSOLVE] + list(flags) + [p], capture_output=True,
                       text=True, timeout=120)
    return r


def parse_solutions(stdout):
    """returns (blocks, terminal): blocks = list of dicts var->int"""
    blocks, terminal, cur = [], None, []
    for ln in stdout.splitlines():
        s = ln.strip()
        if s == "----------":
            blocks.append(cur)
            cur = []
        elif s in ("==========", UNSAT, UNKNOWN):
            terminal = s
        elif s.startswith("%"):
            continue
        elif "=" in s:
            m = re.match(r"^(\w+)\s*=\s*(-?\d+)\s*;\s*$", s)
            if m:
                cur.append((m.group(1), int(m.group(2))))
    return [[kv for kv in blk] for blk in blocks], terminal


def pins():
    bad = []

    def expect(name, text, check, flags=()):
        r = run_fzn(name, text, flags)
        ok, msg = check(r.stdout)
        if not ok:
            bad.append("%s: %s (stdout=%r rc=%d)" % (name, msg, r.stdout[:80], r.returncode))

    # 4-cycle: every start has orbit 4; argmax any of {0..3}; max = 4
    def c1(out):
        blocks, t = parse_solutions(out)
        if t != "==========" or len(blocks) != 1:
            return False, "want one proven-optimal block"
        sol = dict(blocks[0])
        return (sol.get("len") == 4 and sol.get("start") in (0, 1, 2, 3)), \
            "want len=4 start in 0..3, got %s" % sol
    expect("pin_c4", fzn_model([1, 2, 3, 0], "maximize len"), c1)

    # tail+cycle [1,2,3,3]: orbits 4,3,2,1; unique argmax start=0, len=4
    def c2(out):
        blocks, t = parse_solutions(out)
        sol = dict(blocks[0]) if blocks else {}
        return (t == "==========" and sol.get("len") == 4 and sol.get("start") == 0), \
            "want len=4 start=0, got %s" % sol
    expect("pin_tail", fzn_model([1, 2, 3, 3], "maximize len"), c2)

    # -a satisfy on [1,2,3,3]: the full (start,len) relation
    def c3(out):
        blocks, t = parse_solutions(out)
        got = sorted((d["start"], d["len"]) for d in map(dict, blocks))
        want = [(0, 4), (1, 3), (2, 2), (3, 1)]
        return (t == "==========" and got == want), \
            "want %s, got %s" % (want, got)
    expect("pin_all", fzn_model([1, 2, 3, 3], "satisfy"), c3, flags=("-a",))

    # len fixed at 3: only start=1
    def c4(out):
        blocks, t = parse_solutions(out)
        got = sorted(d["start"] for d in map(dict, blocks))
        return (t == "==========" and got == [1]), "want [1], got %s" % got
    expect("pin_lenfix", fzn_model([1, 2, 3, 3], "satisfy", lenfix=3), c4, flags=("-a",))

    # UNSAT pin: len=5 impossible over 4 states
    def c5(out):
        blocks, t = parse_solutions(out)
        return (t == UNSAT and not blocks), "want UNSAT, got %r" % out[:60]
    expect("pin_unsat", fzn_model([1, 2, 3, 3], "satisfy", lenfix=5), c5)

    # restricted start domain 1..3 over [1,2,3,3]: best orbit 3 at start 1
    def c6(out):
        blocks, t = parse_solutions(out)
        sol = dict(blocks[0]) if blocks else {}
        return (t == "==========" and sol.get("len") == 3 and sol.get("start") == 1), \
            "want len=3 start=1, got %s" % sol
    expect("pin_restr", fzn_model([1, 2, 3, 3], "maximize len", lo=1), c6)

    # degenerate n=1 fixed point
    def c7(out):
        blocks, t = parse_solutions(out)
        sol = dict(blocks[0]) if blocks else {}
        return (t == "==========" and sol == {"start": 0, "len": 1}), \
            "want start=0 len=1, got %s" % sol
    expect("pin_n1", fzn_model([0], "maximize len"), c7)

    # decline pin: out-of-range table entry must NOT fabricate an answer
    def c8(out):
        blocks, _ = parse_solutions(out)
        return (not blocks and UNKNOWN in out), \
            "want honest UNKNOWN (decline), got %r" % out[:60]
    expect("pin_decline", fzn_model([1, 2, 7], "maximize len"), c8)

    return bad


def check_real():
    """the actual Unesty/Doing 65536-state automaton from the committed mzn/dzn."""
    dzn = open(os.path.join(ROOT, "examples/procstates/procstates_next.dzn")).read()
    m = re.search(r"\[(.*)\]", dzn, re.S)
    nxt = [int(x) for x in m.group(1).replace("\n", "").split(",")]
    assert len(nxt) == 65536
    bad = []
    fzn = os.path.join(ROOT, "examples/procstates/procstates_orbit.fzn")
    if not os.path.exists(fzn):
        return ["examples/procstates/procstates_orbit.fzn missing"]
    r = subprocess.run([FZNSOLVE, fzn], capture_output=True, text=True, timeout=120)
    blocks, t = parse_solutions(r.stdout)
    if t != "==========" or len(blocks) != 1:
        return ["procstates: want one proven-optimal block, got %r" % r.stdout[:80]]
    sol = dict(blocks[0])
    depth = brute_orbits(nxt)
    bmax = max(depth)
    if sol.get("len") != bmax:
        bad.append("procstates: printed len=%s but brute max is %d" % (sol.get("len"), bmax))
    s = sol.get("start", -1)
    if not (0 <= s < 65536) or depth[s] != sol.get("len"):
        bad.append("procstates: printed start=%s orbit=%s (not the claimed len)"
                   % (s, depth[s] if 0 <= s < 65536 else "out-of-range"))
    return bad


def fuzz(N, seed):
    rng = random.Random(seed)
    bad = []
    for it in range(N):
        n = rng.randint(3, 20)
        nxt = [rng.randrange(n) for _ in range(n)]
        depth = brute_orbits(nxt)
        mode = rng.random()
        if mode < 0.4:
            # maximize: check claimed optimum == brute max and start attains it
            r = run_fzn("fz_%d" % it, fzn_model(nxt, "maximize len"))
            blocks, t = parse_solutions(r.stdout)
            if t != "==========" or len(blocks) != 1:
                bad.append("fuzz %d: maximize not proven-complete: %r" % (it, r.stdout[:60]))
                continue
            sol = dict(blocks[0])
            bmax = max(depth)
            if sol.get("len") != bmax or depth[sol.get("start", 0)] != bmax:
                bad.append("fuzz %d: claimed len=%s start=%s vs brute max=%d (orbit[start]=%s)"
                           % (it, sol.get("len"), sol.get("start"), bmax,
                              depth[sol.get("start", 0)]))
        elif mode < 0.7:
            # -a satisfy: full (start,len) relation
            r = run_fzn("fz_%d" % it, fzn_model(nxt, "satisfy"), flags=("-a",))
            blocks, t = parse_solutions(r.stdout)
            got = sorted((d["start"], d["len"]) for d in map(dict, blocks))
            want = sorted((s, depth[s]) for s in range(n))
            if t != "==========" or got != want:
                bad.append("fuzz %d: -a set mismatch: got %s want %s" % (it, got[:6], want[:6]))
        else:
            # len fixed to a random value: exact filtered start set
            lv = rng.randint(1, n)
            r = run_fzn("fz_%d" % it, fzn_model(nxt, "satisfy", lenfix=lv), flags=("-a",))
            blocks, t = parse_solutions(r.stdout)
            wants = sorted(s for s in range(n) if depth[s] == lv)
            if not wants:
                ok = (t == UNSAT and not blocks)
                if not ok:
                    bad.append("fuzz %d: want UNSAT for len=%d, got %r" % (it, lv, r.stdout[:60]))
            else:
                got = sorted(d["start"] for d in map(dict, blocks))
                if t != "==========" or got != wants:
                    bad.append("fuzz %d: lenfix=%d got %s want %s" % (it, lv, got, wants))
    return bad


def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260816
    os.makedirs(TMP, exist_ok=True)
    bad = pins()
    n_pins_bad = len(bad)
    print("procstates_orbit_verify: pins %s" % ("OK" if not bad else "FAIL"))
    for b in bad:
        print("  pin:", b)
    real = check_real()
    for b in real:
        bad.append(b)
        print("  real:", b)
    fz = fuzz(N, seed)
    bad.extend(fz)
    for b in fz[:10]:
        print("  fuzz:", b)
    print("procstates_orbit_verify: pins_bad=%d real=%s fuzz_N=%d -> WRONG=%d (seed=%d)"
          % (n_pins_bad, "0" if not real else str(len(real)),
             N, len(bad), seed))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
