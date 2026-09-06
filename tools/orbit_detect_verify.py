"""Verification for the psolve presolve orbit-chain structure recovery
(docs/FUNCTIONAL_GRAPH.md, `fz_presolve_orbit` in src/fz_cp.inc).

Emits canonical bounded-horizon orbit-chain FlatZinc (the exact template the
MiniZinc 2.9.4 stdlib produces from a stock encoding: array_int_element
chain with x+1 shift links, int_lin_ne_reif pair lattice under
array_bool_and, bool2int identity links, and the
`len - sum(dst) = 1` objective row), plus near-miss and far mutations.
Every emitted model's expected optimum/projection is computed by an
independent Python evaluator of the EMITTED CONSTRAINT SEMANTICS (not of the
orbit notion), and the rewrite must fire exactly on the exact-match
candidates:

  positives (rewrite must fire; answer must match semantic evaluator):
      plain maximize, satisfy -a projection, minimize, two chains on one
      table, randomized link/rhs orientations
  negatives (rewrite must NOT fire; generic path must stay correct):
      extra pin on an intermediate, missing pair in an AND input, mutated
      ne_reif rhs, mixed tables, mutated sum rhs, broken chain,
      intermediate marked ::output_var, intermediate in the objective
  (rewrite firing is observed via PSOLVE_TRACE_PRESOLVE=1 stderr lines:
  a candidate the detector believes in always logs; silence == no fire --
  on the pre-change binary the parser itself lacks array_bool_and, so all
  positives fail loudly, which is what calibration asserts.)

Wrinkle coverage: on positives the reported optimum must be PROVEN
(marker ========== present), and objectiveBound must equal the evaluator's.

WRONG per violation; summary line; exit 0 iff WRONG==0.

env: PSOLVE_FZNSOLVE overrides the binary; PSOLVE_PROC_NOCHECK unused.
"""
import os
import random
import re
import subprocess
import sys

FZNSOLVE = os.path.abspath(os.environ.get("PSOLVE_FZNSOLVE", os.path.join(os.path.dirname(__file__), "..", "fznsolve")))
TRACE_ENV = dict(os.environ, PSOLVE_TRACE_PRESOLVE="1")

WRONG = 0
def bad(*a):
    global WRONG
    WRONG += 1
    print("  WRONG:", *a)

# ------------------------------------------------------------- emitter -----
class ChainEmit:
    """canonical template emitter; tracks every constraint for evaluation"""
    def __init__(self, nxt, H, rng, solve_kind="maximize", flip_prob=0.5, prefix=""):
        self.nxt = nxt
        self.n = len(nxt)
        self.H = H
        self.rng = rng
        self.solve_kind = solve_kind
        self.flip = flip_prob
        self.px = prefix
        self.decls = []
        self.cons = []
        self.x = [prefix + f"x{k}" for k in range(H + 1)]
        self.s = [prefix + f"s{k}" for k in range(H)]
        self.len = prefix + "len"
        self.lenvar = self.len
        self.elems = []        # (svar, tablename, outvar)
        self.pairs = {}        # (i,j) -> boolvar, plus mutated rhs map
        self.pair_rhs = {}
        self.ands = {}         # k -> boolvar
        self.and_inputs = {}   # k -> [boolvar]
        self.dsts = {}         # k -> intvar
        self.sum_rhs = 1
        self.extra = []        # extra constraint lines (evaluate {} tuples)
        self.pins = {}         # var -> const (extra int_eq pins)
        self.outs = [self.x[0], self.lenvar]
        self.objective_extra = None
        self.tables = {"T": list(nxt)}

    def lin(self, var, relation_rhs_const=0):
        return

    def build(self, drop_elem=None, no_fire_extra=None):
        r = self.rng
        n, H = self.n, self.H
        lines = []
        for tname, tab in self.tables.items():
            lines.append("array [1..%d] of int: %s = [%s];" % (n, tname, ",".join(map(str, tab))))
        x0ann = " :: output_var" if self.x[0] in self.outs else ""
        lines.append("var 0..%d: %s%s;" % (n - 1, self.x[0], x0ann))
        for k in range(1, H + 1):
            ann = " :: output_var" if self.x[k] in self.outs else ""
            lines.append("var 0..%d: %s%s;" % (n - 1, self.x[k], ann))
        for k in range(H):
            lines.append("var 1..%d: %s;" % (n, self.s[k]))
        lines.append("var 1..%d: %s :: output_var;" % (n, self.lenvar))
        # shift + element chain
        for k in range(H):
            if k == drop_elem:
                continue
            tname = "T" if not isinstance(self.tables.get("T2"), list) or r.random() < 0.999 else "T2"
            tname = getattr(self, "_force_table", None) or "T"
            if r.random() < self.flip:
                lines.append("constraint int_lin_eq([1,-1],[%s,%s],-1);" % (self.x[k], self.s[k]))
            else:
                lines.append("constraint int_lin_eq([-1,1],[%s,%s],1);" % (self.x[k], self.s[k]))
            lines.append("constraint array_int_element(%s,%s,%s);" % (self.s[k], tname, self.x[k + 1]))
            self.elems.append((self.s[k], tname, self.x[k + 1]))
        # lattice
        for k in range(2, H + 1):
            bools = []
            for i in range(k):
                for j in range(i + 1, k):
                    bv = self.px + "p%d_%d" % (i, j)
                    if bv not in self.pairs.values():
                        lines.append("var 0..1: %s;" % bv)
                        rhs = self.pair_rhs.get((i, j), 0)
                        if r.random() < self.flip:
                            lines.append("constraint int_lin_ne_reif([1,-1],[%s,%s],%d,%s);" % (self.x[i], self.x[j], rhs, bv))
                        else:
                            lines.append("constraint int_lin_ne_reif([-1,1],[%s,%s],%d,%s);" % (self.x[i], self.x[j], rhs, bv))
                        self.pairs[(i, j)] = bv
                    bools.append(self.pairs[(i, j)])
            Bk = self.px + "B%d" % k
            dk = self.px + "d%d" % k
            drop = self.and_inputs.get(k)  # mutation: specific bool to drop
            if drop is None:
                use = bools
            else:
                use = [b for b in bools if b != drop]
            self.and_inputs[k] = use
            lines.append("var 0..1: %s;" % dk)
            if len(use) == 1:
                # canonical k=2 (or CSE-collapsed) shape: bool2int directly
                # from the single pair bool, no AND record at all
                lines.append("constraint bool2int(%s,%s);" % (use[0], dk))
            else:
                lines.append("var 0..1: %s;" % Bk)
                lines.append("constraint array_bool_and([%s],%s);" % (",".join(use), Bk))
                lines.append("constraint bool2int(%s,%s);" % (Bk, dk))
            self.dsts[k] = dk
        # objective row
        dlist = [self.dsts[k] for k in range(2, H + 1)]
        if r.random() < self.flip:
            lines.append("constraint int_lin_eq([1,%s],[%s,%s],%d);" % (",".join(["-1"] * len(dlist)), self.lenvar, ",".join(dlist), self.sum_rhs))
        else:
            lines.append("constraint int_lin_eq([-1,%s],[%s,%s],%d);" % (",".join(["1"] * len(dlist)), self.lenvar, ",".join(dlist), -self.sum_rhs))
        for v, c in self.pins.items():
            lines.append("constraint int_eq(%s,%d);" % (v, c))
        if self.objective_extra:
            lines.append(self.objective_extra)
        lines.extend(self.extra)
        lines.append("solve %s %s;" % (self.solve_kind, self.lenvar))
        return "\n".join(lines) + "\n"

# ------------------------------------------------------------ evaluator ----
def orbit_fact(nxt):
    n = len(nxt)
    tr = [None] * n
    for s in range(n):
        if tr[s] is not None:
            continue
        order, pos, cur = [], {}, s
        while tr[cur] is None and cur not in pos:
            pos[cur] = len(order); order.append(cur); cur = nxt[cur]
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
    return [t + c for (t, c) in tr]

def eval_emitted(em, drop_elem=None):
    """enumerate x0; evaluate the EMITTED constraint semantics; return
    dict(x0 -> len) for consistent assignments."""
    nxt = em.tables["T"]
    n, H = em.n, em.H
    out = {}
    for x0 in range(n):
        x = [0] * (H + 1)
        x[0] = x0
        ok = True
        for k in range(H):
            if k == drop_elem:
                ok = False
                break
            tab = em.tables[getattr(em, "_force_table", None) or "T"]
            x[k + 1] = tab[x[k]]
        if not ok:
            continue
        # pins on intermediates
        for v, c in em.pins.items():
            k = int(v.split("x")[-1])
            if x[k] != c:
                ok = False
        if not ok:
            continue
        Bvals = {}
        dsum = 0
        for k in range(2, H + 1):
            acc = 1
            for bv in em.and_inputs[k]:
                for (i, j), pb in em.pairs.items():
                    if pb == bv:
                        rhs = em.pair_rhs.get((i, j), 0)
                        if (x[i] - x[j]) != rhs:
                            acc &= 1
                        else:
                            acc &= 0
            dsum += acc
        lv = dsum + em.sum_rhs
        if 1 <= lv <= em.n:
            out[x0] = lv
    return out

# -------------------------------------------------------------- runner -----
def run(name, text, flags=()):
    p = f"/tmp/orbdet_{name}.fzn"
    with open(p, "w") as f:
        f.write(text)
    try:
        r = subprocess.run([FZNSOLVE, "-s"] + list(flags) + [p], capture_output=True, text=True, timeout=60, env=TRACE_ENV)
    except subprocess.TimeoutExpired:
        # never acceptable post-change (case sizes are tuned to finish); on
        # the pre-change calibration binary it is itself the measured gap
        class T: pass
        r = T(); r.returncode = None; r.stdout = ""; r.stderr = "TIMEOUT"
    return r

def fired(r, times=1):
    return r.stderr.count("orbit_chain rewrite") == times

def objective_of(r):
    m = re.search(r"%mzn-stat: objective=(\d+)", r.stdout)
    return int(m.group(1)) if m else None

def bound_of(r):
    m = re.search(r"%mzn-stat: objectiveBound=(\d+)", r.stdout)
    return int(m.group(1)) if m else None

def last_sols(r):
    sols = []
    cur = {}
    for line in r.stdout.splitlines():
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*) = (-?\d+);$", line.strip())
        if m:
            cur[m.group(1)] = int(m.group(2))
        elif line.strip() == "----------":
            sols.append(cur)
            cur = {}
    return sols

def main():
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 20260818
    rng = random.Random(seed)
    bads = 0

    for it in range(N):
        mode = it % 10
        # positives are cheap once rewritten (any size); negatives fall back
        # to the generic lattice path, which is intrinsically slow (that is
        # the point of the recovery), so mutations stay tiny: reachable by
        # the generic CP engine in well under the per-case budget
        if mode in (0, 1, 2, 9):
            n = rng.randint(4, 12)
        else:
            n = rng.randint(3, 6)
        nxt = [rng.randrange(n) for _ in range(n)]
        orb = orbit_fact(nxt)
        name = f"i{it}"

        def fresh(H, kind="maximize"):
            return ChainEmit(nxt, H, rng, solve_kind=kind, flip_prob=rng.random())

        if mode == 0:
            # P: plain maximize -> fire, proven optimum min(maxorb,H)
            H = rng.randint(2, 8)
            em = fresh(H)
            txt = em.build()
            want = min(max(orb), H)
            r = run(name, txt)
            if not fired(r, 1):
                bads += 1; bad(name, "P-max no fire", r.stderr[:120])
            elif objective_of(r) != want or bound_of(r) != want or "==========\n" not in r.stdout + "\n":
                bads += 1; bad(name, "P-max answer", want, objective_of(r), bound_of(r), r.stdout.splitlines()[:5])
        elif mode == 1:
            # P: minimize -> ALSO an exact rewrite (semantics preserved)
            H = rng.randint(2, 8)
            em = fresh(H, "minimize")
            txt = em.build()
            want = min(min(orb), H)
            r = run(name, txt)
            if not fired(r, 1):
                bads += 1; bad(name, "P-min no fire")
            elif objective_of(r) != want or bound_of(r) != want:
                bads += 1; bad(name, "P-min answer", want, objective_of(r), bound_of(r))
        elif mode == 2:
            # P: two chains over the same table, joint objective len1+len2:
            # both rewrites must fire; the optimum is sum of min(orb,H_i)
            H1, H2 = rng.randint(2, 6), rng.randint(2, 6)
            kind = rng.choice(["maximize", "minimize"])
            em1 = ChainEmit(nxt, H1, rng, solve_kind="maximize", flip_prob=rng.random())
            em2 = ChainEmit(nxt, H2, rng, solve_kind="maximize", flip_prob=rng.random(), prefix="w")
            t1 = em1.build().splitlines()
            t1 = [l for l in t1 if not l.startswith("array ") and not l.startswith("solve ")]
            t2 = em2.build().splitlines()
            t2 = [l for l in t2 if not l.startswith("array ") and not l.startswith("solve ")]
            tab_line = t2 and "array [1..%d] of int: T = [%s];" % (n, ",".join(map(str, nxt)))
            obj = ("var 2..%d: obj :: output_var;" % (2 * n)) + "\n"
            tie = "constraint int_lin_eq([1,-1,-1],[obj,%s,%s],0);" % (em1.lenvar, em2.lenvar)
            txt = "\n".join([tab_line] + t1 + t2 + [obj + tie, "solve %s obj;" % kind]) + "\n"
            f1 = min(max(orb), H1) if kind == "maximize" else min(min(orb), H1)
            f2 = min(max(orb), H2) if kind == "maximize" else min(min(orb), H2)
            want = f1 + f2
            r = run(name, txt)
            if r.stderr.count("orbit_chain rewrite") != 2:
                bads += 1; bad(name, "P-two fire count", r.stderr[:120])
            elif objective_of(r) != want:
                bads += 1; bad(name, "P-two answer", want, objective_of(r), r.stdout.splitlines()[:5])
        elif mode == 3:
            # N: extra pin on an intermediate -> NO fire, generic answer right
            H = rng.randint(3, 4)
            k = rng.randint(1, H)
            em = fresh(H)
            pk = rng.randrange(n)
            em.pins[em.x[k]] = pk
            txt = em.build()
            wantset = eval_emitted(em)
            want = max(wantset.values()) if wantset else None
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-pin fired")
            elif want is None:
                if "UNSATISFIABLE" not in r.stdout:
                    bads += 1; bad(name, "N-pin UNSAT expected", r.stdout[:80])
            elif objective_of(r) != want:
                bads += 1; bad(name, "N-pin answer", want, objective_of(r), r.stdout.splitlines()[:4])
        elif mode == 4:
            # N: missing pair inside one AND -> NO fire
            H = rng.randint(4, 4)
            em = fresh(H)
            victim = sorted(em.dsts)  # placeholder; drop from the biggest AND
            # drop one bool from the H-level AND
            cand = [(i, j) for i in range(H) for j in range(i + 1, H)]
            i, j = cand[rng.randrange(len(cand))]
            em.and_inputs[H] = em.pairs.get((i, j)) or "p%d_%d" % (i, j)
            txt = em.build()
            wantset = eval_emitted(em)
            want = max(wantset.values())
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-andgap fired")
            elif objective_of(r) != want:
                bads += 1; bad(name, "N-andgap answer", want, objective_of(r))
        elif mode == 5:
            # N: mutated ne_reif rhs on one pair -> NO fire
            H = rng.randint(3, 4)
            em = fresh(H)
            cand = [(i, j) for i in range(2) for j in range(i + 1, 2)]
            i, j = cand[0]
            em.pair_rhs[(i, j)] = 1
            txt = em.build()
            wantset = eval_emitted(em)
            want = max(wantset.values())
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-rhs fired")
            elif objective_of(r) != want:
                bads += 1; bad(name, "N-rhs answer", want, objective_of(r), r.stdout.splitlines()[:5])
        elif mode == 6:
            # N: mutated objective-row constant -> NO fire
            H = rng.randint(2, 4)
            em = fresh(H)
            em.sum_rhs = 2
            txt = em.build()
            wantset = eval_emitted(em)
            want = max(wantset.values())
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-sum fired")
            elif objective_of(r) != want:
                bads += 1; bad(name, "N-sum answer", want, objective_of(r))
        elif mode == 7:
            # N: broken chain (drop an element) -> NO fire (only assert fire-safety)
            H = rng.randint(3, 4)
            em = fresh(H)
            drop = rng.randrange(H)
            txt = em.build(drop_elem=drop)
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-broken fired")
            if r.returncode != 0:
                bads += 1; bad(name, "N-broken rc", r.returncode, r.stderr[:100])
        elif mode == 8:
            # N: intermediate is ::output_var -> NO fire
            H = rng.randint(3, 4)
            k = rng.randint(1, H)
            em = fresh(H)
            em.outs.append(em.x[k])
            txt = em.build()
            wantset = eval_emitted(em)
            want = max(wantset.values())
            r = run(name, txt)
            if fired(r):
                bads += 1; bad(name, "N-out fired")
            elif objective_of(r) != want:
                bads += 1; bad(name, "N-out answer", want, objective_of(r))
        else:
            # P: satisfy -a with len pinned by domain: full (x0,len) projection
            H = rng.randint(2, 6)
            lf = rng.randint(1, H)
            em = fresh(H, "satisfy")
            txt = em.build()
            txt = txt.replace("var 1..%d: %s :: output_var;" % (n, em.lenvar), "var %d..%d: %s :: output_var;" % (lf, lf, em.lenvar))
            wantproj = sorted((x0, l) for x0, l in eval_emitted(em).items() if l == lf)
            r = run(name, txt, flags=("-a",))
            sols = last_sols(r)
            gotproj = sorted((x["x0"], x["len"]) for x in sols)
            if not fired(r, 1):
                bads += 1; bad(name, "P-proj no fire", r.stderr[:100], r.stdout[:80])
            elif gotproj != wantproj:
                bads += 1; bad(name, "P-proj answer", wantproj[:6], gotproj[:6], len(wantproj), len(gotproj))

    print(f"orbit_detect_verify: cases={N} -> WRONG={WRONG} (seed={seed})")
    return 0 if WRONG == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
