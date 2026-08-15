CC      ?= gcc
OPT     ?= -O3
# Architecture.  Default to native auto-detection: with -march=native the
# compiler emits AVX-512 only on machines that support it and the scalar
# fallback otherwise, so the binary runs on the machine it was built for.
# To build for a specific baseline set, e.g.:
#   make ARCH="-march=x86-64 -mavx2"
ARCH    ?= -march=native

# Performance flags.  NOTE: -mavx512f/-mfma are NOT forced here; they are only
# active if the chosen ARCH enables them.  Forcing them unconditionally would
# make the binary SIGILL on CPUs without AVX-512.
#
# -ffast-math is OFF by default: for a numerically-sensitive LP/QP/MIP solver
# it can reorder FP ops, change NaN/Inf behaviour, and alter signed zero, all
# of which matter for feasibility checks and `inf` sentinel bounds.  Enable it
# only for a non-correctness benchmark build with:  make FAST_MATH=1
FAST_MATH ?= 0
ifeq ($(FAST_MATH),1)
PERF    = -funroll-loops -fno-math-errno -ffast-math
else
PERF    = -funroll-loops -fno-math-errno
endif

# Defensive hardening: stack protector, FORTIFY_SOURCE, format checks,
# and PIE + RELRO so ROP/GOT-overwrite attacks are harder.
HARDEN  = -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
          -Wformat=2 -Werror=format-security -fPIE
LDFLAGS = -pie -Wl,-z,relro,-z,now -Wl,--as-needed -Wl,-z,noexecstack

CFLAGS   = -std=gnu11 -Wall -Wextra $(OPT) $(ARCH) $(PERF) $(HARDEN)
LDLIBS   = -lm

SRC = src/err.c src/kernels.c src/lu.c src/splu.c src/solver.c src/parser.c src/main.c
OBJ = $(SRC:.c=.o)
QPSRC = src/err.c src/qp.c src/lu.c src/kernels.c
QPOBJ = $(QPSRC:.c=.o)

# Library (for embedding in other projects, e.g. SmazkaVG).
#   make lib   ->  libpsolve.a  (all solvers: LP, QP, MIP, PGS)
#   make liblp ->  libpsolve-lp.a   (LP only)
#   make libqp ->  libpsolve-qp.a   (QP only)
LIB_SRC = src/err.c src/kernels.c src/lu.c src/splu.c src/solver.c src/parser.c src/qp.c src/mip.c src/fx.c src/pgs.c src/pgs_fixed.c src/fzn.c
LIB_OBJ = $(LIB_SRC:.c=.o)
LP_LIB_SRC = src/err.c src/kernels.c src/lu.c src/splu.c src/solver.c src/parser.c
LP_LIB_OBJ = $(LP_LIB_SRC:.c=.o)
QP_LIB_SRC = src/err.c src/qp.c src/lu.c src/kernels.c src/pgs.c src/pgs_fixed.c
QP_LIB_OBJ = $(QP_LIB_SRC:.c=.o)

all: lpsolve qpsolve mipsolve pgsbench pgfbench fznsolve fxsolve

bench_mzn: fznsolve
	python3 tools/mzn_bench.py

install_mzn:
	mkdir -p $(HOME)/.minizinc/solvers
	cp share/minizinc/solvers/psolve.msc $(HOME)/.minizinc/solvers/psolve.msc


lpsolve: $(OBJ)
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

qpsolve: src/err.o src/qp.o src/lu.o src/kernels.o tools/qpsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

mipsolve: src/err.o src/mip.o src/lu.o src/splu.o src/solver.o src/kernels.o src/parser.o src/fx.o tools/mipsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

# Real-time 2D-physics kernel: projected Gauss-Seidel boxed-QP
pgsbench: src/pgs.o tools/pgbench.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

src/pgs.o: src/pgs.c src/pgs.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/pgbench.o: tools/pgbench.c src/pgs.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# PGS vs LP/QP benchmark (which solver foundation fits real-time physics)
pgs_vs_lp: tools/pgs_vs_lp.o src/err.o src/pgs.o src/pgs_fixed.o src/qp.o src/lu.o src/kernels.o src/splu.o src/solver.o src/parser.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

tools/pgs_vs_lp.o: tools/pgs_vs_lp.c src/pgs.h src/pgs_fixed.h src/qp.h src/solver.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# FlatZinc reader + solver bridge (Phase 3)
fznsolve: src/fzn.o src/mip.o src/err.o src/solver.o src/splu.o src/lu.o src/kernels.o src/parser.o src/fx.o tools/fznsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

src/fzn.o: src/fzn.c src/fzn.h src/solver.h src/err.h src/fz_cp.inc
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/fznsolve.o: tools/fznsolve.c src/fzn.h src/err.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# Fixed-point physics kernel
pgfbench: src/pgs_fixed.o tools/pgfbench.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

src/pgs_fixed.o: src/pgs_fixed.c src/pgs_fixed.h src/pgs.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/pgfbench.o: tools/pgfbench.c src/pgs_fixed.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

src/mip.o: src/mip.c src/mip.h src/solver.h src/err.h src/fx.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/mipsolve.o: tools/mipsolve.c src/mip.h src/parser.h src/err.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# Fixed-point (exact rational) LP solver (src/fx.c)
fxsolve: src/err.o src/fx.o tools/fxsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

src/fx.o: src/fx.c src/fx.h src/fx_core.inc src/err.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/fxsolve.o: tools/fxsolve.c src/fx.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# Benchmark: double revised-simplex vs fixed-point exact simplex
fx_bench: tools/fx_bench.o src/err.o src/kernels.o src/lu.o src/splu.o src/solver.o src/parser.o src/fx.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

tools/fx_bench.o: tools/fx_bench.c src/solver.h src/parser.h src/fx.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# Library targets ------------------------------------------------------
# Static archives of the solver cores (no main()).  Headers to use from a
# consuming project: src/solver.h (LP), src/qp.h (QP), src/mip.h (MIP),
# src/pgs.h (real-time physics).
# Example consumer link:
#   cc -I psolve/src -DSMZ_HAVE_PSOLVE app.c psolve/libpsolve.a -lm
lib: libpsolve.a
liblp: libpsolve-lp.a
libqp: libpsolve-qp.a

libpsolve.a: $(LIB_OBJ)
	ar rcs $@ $^

libpsolve-lp.a: $(LP_LIB_OBJ)
	ar rcs $@ $^

libpsolve-qp.a: $(QP_LIB_OBJ)
	ar rcs $@ $^

# AddressSanitizer + UndefinedBehaviorSanitizer debug build (not for
# production): catches memory-safety and UB bugs on malformed input.
asan: clean
	$(MAKE) OPT="-O1 -g" PERF="-fno-omit-frame-pointer" \
		CFLAGS_EXTRA="-fsanitize=address,undefined" all

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# src/main.o is the LP CLI: it shares the millisecond-precision time-limit
# helper (tools/tlimit.h) with the other CLI drivers, so give it that include
# path too.
src/main.o: src/main.c src/parser.h src/solver.h src/err.h tools/tlimit.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -I tools -c -o $@ $<

clean:
	rm -f lpsolve qpsolve mipsolve pgsbench pgfbench pgs_vs_lp fznsolve fxsolve fx_bench src/*.o tools/*.o libpsolve.a libpsolve-lp.a libpsolve-qp.a

.PHONY: all asan clean lib liblp libqp
