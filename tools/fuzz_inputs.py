#!/usr/bin/env python3
"""Fuzz `lpsolve` and `qpsolve` with random malformed and valid inputs.

Builds ASAN/UBSAN-instrumented binaries (or uses existing ones), generates
random .lp/.qp files (malformed and well-formed), and runs each through the
solver, aborting if the sanitizer reports any memory-safety or UB issue.

Usage:
    python3 tools/fuzz_inputs.py [--iters N] [--seed S] [--bin DIR]
"""
import random, subprocess, os, sys, shutil

def parse_args(argv):
    iters = 200; seed = 0; bindir = None
    i = 0
    while i < len(argv):
        if argv[i] == '--iters': iters = int(argv[i+1]); i += 2
        elif argv[i] == '--seed': seed = int(argv[i+1]); i += 2
        elif argv[i] == '--bin': bindir = argv[i+1]; i += 2
        else: i += 1
    return iters, seed, bindir

def build_asan(bindir):
    os.makedirs(bindir, exist_ok=True)
    base = ['gcc','-O1','-g','-march=native','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-I','src','-I','tools']
    lp = base + ['src/err.c','src/kernels.c','src/lu.c','src/splu.c','src/solver.c',
                 'src/parser.c','src/main.c','-o',os.path.join(bindir,'lpsolve_asan'),'-lm']
    qp = base + ['src/err.c','tools/qpsolve.c','src/qp.c','src/lu.c','src/kernels.c',
                 '-o',os.path.join(bindir,'qpsolve_asan'),'-lm']
    subprocess.run(lp, check=True)
    subprocess.run(qp, check=True)

def rand_lp(rng):
    sense = rng.choice(['maximize','minimize','max','garbage',''])
    n = rng.choice([0,1,2,5,10,-1,-5,1000000,3])
    m = rng.choice([0,1,2,5,-1,3,1000000])
    L=[sense, f"{n} {m}"]
    nn=max(0,min(n,8)); mm=max(0,min(m,8))
    L.append(" ".join(str(rng.uniform(-10,10)) for _ in range(nn)))
    L.append(" ".join(str(rng.uniform(-5,20)) for _ in range(mm)))
    L.append("".join(rng.choice(['<','>','=','<=','>=','x','']) for _ in range(rng.choice([mm,mm*2,mm+3]))))
    for _ in range(nn):
        L.append(f"{rng.choice(['0','-inf','inf','x','1e400','5'])} {rng.choice(['inf','10','-inf','3'])}")
    nz = rng.choice([0,1,3,10,-1,100])
    L.append(str(nz))
    for _ in range(max(0,min(nz,12))):
        L.append(f"{rng.choice([0,rng.randint(-5,mm+5),mm+99,-3])} {rng.choice([0,rng.randint(-5,nn+5),nn+99,-2])} {rng.uniform(-5,5)}")
    if rng.random()<0.3: L.append("trailing junk")
    return "\n".join(L)

def rand_qp(rng):
    n = rng.choice([0,1,3,-2,100000,8193])
    m = rng.choice([0,1,2,-1,100000])
    nn=max(0,min(n,5)); mm=max(0,min(m,5))
    L=[f"{n} {m}"]
    L.append(" ".join(str(rng.uniform(-5,5)) for _ in range(nn)))
    for _ in range(nn):
        L.append(" ".join(str(rng.uniform(-5,5)) for _ in range(nn)))
    for _ in range(mm):
        L.append(" ".join(str(rng.uniform(-5,5)) for _ in range(nn)))
    L.append(" ".join(str(rng.uniform(-5,5)) for _ in range(mm)))
    if rng.random()<0.2: L.append("junk")
    return "\n".join(L)

def run(binary, path, tag, it):
    r = subprocess.run([binary, path], capture_output=True, timeout=15)
    err = r.stderr.decode('utf-8','replace')
    if 'ERROR: AddressSanitizer' in err or 'runtime error:' in err or 'UndefinedBehaviorSanitizer' in err:
        print(f"[!] {tag} ASAN/UBSAN hit at iteration {it}")
        print(err[:1200])
        return False
    return True

def rejected(binary, path, content):
    """Non-finite and structurally invalid models must fail in the parser."""
    with open(path, 'w') as f:
        f.write(content)
    r = subprocess.run([binary, path], capture_output=True, timeout=15)
    return r.returncode != 0

def main():
    iters, seed, bindir = parse_args(sys.argv[1:])
    use_existing = bindir is not None
    if bindir is None: bindir = '/tmp/psolve_fuzz'
    rng = random.Random(seed)
    if (not use_existing or
        not (os.path.exists(os.path.join(bindir,'lpsolve_asan')) and
             os.path.exists(os.path.join(bindir,'qpsolve_asan')))):
        print("building ASAN binaries...")
        build_asan(bindir)
    lpb = os.path.join(bindir,'lpsolve_asan')
    qpb = os.path.join(bindir,'qpsolve_asan')
    os.makedirs('/tmp/psolve_fuzz_in', exist_ok=True)

    # Fixed regressions: early parser failures used to jump over pointer
    # initializers before cleanup, and NaN coefficients could reach the solver
    # and be reported as an "OPTIMAL" objective of NaN.
    bad_lp = [
        "garbage\n",
        "maximize\n1 0\nnan\n\n0 inf\n0\n",
        "maximize\n1 1\n0\nnan\n<\n0 1\n0\n",
        "maximize\n1 1\n0\n0\n<\n0 1\n1\n0 0 nan\n",
    ]
    for i, content in enumerate(bad_lp):
        if not rejected(lpb, '/tmp/psolve_fuzz_in/reject.lp', content):
            print(f"[!] LP parser accepted fixed invalid case {i}")
            return 1
    if not rejected(qpb, '/tmp/psolve_fuzz_in/reject.qp', "1 0\nnan\n1\n"):
        print("[!] QP parser accepted a non-finite coefficient")
        return 1

    for it in range(iters):
        with open('/tmp/psolve_fuzz_in/t.lp','w') as f: f.write(rand_lp(rng))
        if not run(lpb,'/tmp/psolve_fuzz_in/t.lp','LP',it): return 1
        with open('/tmp/psolve_fuzz_in/t.qp','w') as f: f.write(rand_qp(rng))
        if not run(qpb,'/tmp/psolve_fuzz_in/t.qp','QP',it): return 1
    print(f"fuzz OK: {iters} malformed LP + {iters} malformed QP inputs, no sanitizer errors")
    return 0

if __name__ == '__main__':
    sys.exit(main())
