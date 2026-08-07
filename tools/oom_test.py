#!/usr/bin/env python3
"""Out-of-memory injection test.

README's "Security & untrusted input" section claims that allocations are
checked and that failures are reported through the psolve_try()/psolve_fail()
protocol rather than by dereferencing NULL.  This test proves it: an LD_PRELOAD
shim (tools/oomlib.c) makes the Nth and every later allocation fail, and the
driver replays every binary once per N.  A run passes only if the process exits
by itself -- never SIGSEGV (unchecked allocation), never SIGABRT (psolve_fail()
with no handler installed), never a hang.

The shim counts *all* allocations, including the ones libc makes internally
(fopen buffers, strndup, ...), so this also catches allocating libc calls whose
result is used unchecked.

Coverage is not guessed: each case is first run once with PSOLVE_OOM_COUNT=1 to
census how many allocations it performs, and the injection sweep then covers
that whole range.  Cases with very many allocations (the QP active-set loop
reallocates per iteration) are sampled on a fixed stride unless --full is given
so the suite stays fast; the sample is deterministic.

usage: oom_test.py [--full] [max_runs_per_case]
"""
import os, subprocess, sys, shutil, signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = "/tmp/psolve_oomlib.so"
QP_PATH = "/tmp/psolve_oom_qp.qp"

QP = """2 1
2 0
0 2
-2 -5
1 1
3
"""

CASES = [
    ["./lpsolve", "examples/prodplan.lp", "--print"],
    ["./lpsolve", "examples/transport.lp"],
    ["./lpsolve", "examples/free_vars.lp", "--print"],
    ["./qpsolve", QP_PATH],
    ["./mipsolve", "examples/prodplan.lp", "2", "0", "1", "--print"],
    ["./fxsolve", "examples/diet.lp", "--print"],
    ["./fxsolve", "examples/exact.lp", "--print"],
] + [["./fznsolve", os.path.join("examples/fzn", f)]
     for f in sorted(os.listdir(os.path.join(ROOT, "examples/fzn")))
     if f.endswith(".fzn")]

# Default budget of injection points per case.  Everything in the suite except
# qpsolve and fzn/table_opt is fully covered by this.
DEFAULT_MAX_RUNS = 900


def sig_name(rc):
    s = -rc
    try:
        return signal.Signals(s).name
    except ValueError:
        return "signal %d" % s


def census(exe, argv, env0):
    """Total number of allocations in an unperturbed run."""
    env = dict(env0, LD_PRELOAD=LIB, PSOLVE_OOM_COUNT="1")
    try:
        p = subprocess.run([exe] + argv, cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return None
    for line in p.stderr.splitlines():
        if line.startswith("PSOLVE_ALLOCS "):
            return int(line.split()[1])
    return None


def main():
    args = [a for a in sys.argv[1:]]
    full = "--full" in args
    if full:
        args.remove("--full")
    max_runs = int(args[0]) if args else DEFAULT_MAX_RUNS

    if not sys.platform.startswith("linux"):
        print("oom_test: SKIPPED (not Linux)")
        return 0
    if shutil.which("gcc") is None:
        print("oom_test: SKIPPED (no gcc)")
        return 0

    src = os.path.join(ROOT, "tools", "oomlib.c")
    r = subprocess.run(["gcc", "-shared", "-fPIC", "-O1", "-o", LIB, src, "-ldl"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print("oom_test: SKIPPED (cannot build shim)\n" + r.stderr[:400])
        return 0

    with open(QP_PATH, "w") as f:
        f.write(QP)

    env0 = dict(os.environ)
    env0.pop("PSOLVE_OOM_AFTER", None)
    env0.pop("PSOLVE_OOM_COUNT", None)

    bad, runs, covered, total_allocs = [], 0, 0, 0
    for case in CASES:
        exe = os.path.join(ROOT, case[0].lstrip("./"))
        if not os.path.exists(exe):
            continue
        argv = case[1:]
        n_alloc = census(exe, argv, env0)
        if n_alloc is None:
            bad.append((case[0], -1, "census failed"))
            continue
        total_allocs += n_alloc
        points = list(range(0, n_alloc + 1))
        if not full and len(points) > max_runs:
            # keep the first half of the budget contiguous (setup / parsing,
            # where each allocation is a distinct site) and stride the rest.
            head = max_runs // 2
            tail = points[head:]
            step = (len(tail) + (max_runs - head) - 1) // (max_runs - head)
            points = points[:head] + tail[::step]
        covered += len(points)
        for n in points:
            env = dict(env0, LD_PRELOAD=LIB, PSOLVE_OOM_AFTER=str(n))
            try:
                p = subprocess.run([exe] + argv, cwd=ROOT, env=env,
                                   capture_output=True, text=True, timeout=60)
            except subprocess.TimeoutExpired:
                bad.append((case[0] + " " + " ".join(argv), n, "TIMEOUT"))
                continue
            runs += 1
            if p.returncode < 0:
                bad.append((case[0] + " " + " ".join(argv), n,
                            "killed by " + sig_name(p.returncode)))
    print("oom_test: %d injection points over %d allocations, runs=%d failures=%d"
          % (covered, total_allocs, runs, len(bad)))
    for b in bad[:20]:
        print("  %s  fail-after=%d  %s" % b)
    if len(bad) > 20:
        print("  ... and %d more" % (len(bad) - 20))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
