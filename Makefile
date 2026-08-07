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
PERF    = -funroll-loops -fno-math-errno -ffast-math

# Defensive hardening (F-08): stack protector, FORTIFY_SOURCE, format checks,
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

all: lpsolve qpsolve mipsolve

lpsolve: $(OBJ)
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

qpsolve: src/err.o src/qp.o src/lu.o src/kernels.o tools/qpsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

mipsolve: src/err.o src/mip.o src/lu.o src/splu.o src/solver.o src/kernels.o src/parser.o tools/mipsolve.o
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -o $@ $^ $(LDFLAGS) $(LDLIBS)

src/mip.o: src/mip.c src/mip.h src/solver.h src/err.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

tools/mipsolve.o: tools/mipsolve.c src/mip.h src/parser.h src/err.h
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

# AddressSanitizer + UndefinedBehaviorSanitizer debug build (not for
# production): catches memory-safety and UB bugs on malformed input.
asan: clean
	$(MAKE) OPT="-O1 -g" PERF="-fno-omit-frame-pointer" \
		CFLAGS_EXTRA="-fsanitize=address,undefined" all

%.o: %.c
	$(CC) $(CFLAGS) $(CFLAGS_EXTRA) -I src -c -o $@ $<

clean:
	rm -f lpsolve qpsolve mipsolve src/*.o tools/*.o

.PHONY: all asan clean
