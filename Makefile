CC      ?= gcc
OPT     ?= -O3
ARCH    ?= -march=native
CFLAGS   = -std=gnu11 -Wall -Wextra -O3 $(ARCH) -mavx512f -mfma -funroll-loops \
           -fno-math-errno -ffast-math
LDLIBS   = -lm

SRC = src/kernels.c src/lu.c src/splu.c src/solver.c src/parser.c src/main.c
OBJ = $(SRC:.c=.o)
QPSRC = src/qp.c src/lu.c src/kernels.c
QPOBJ = $(QPSRC:.c=.o)

all: lpsolve qpsolve

lpsolve: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

qpsolve: src/qp.o src/lu.o src/kernels.o tools/qpsolve.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/qp.o: src/qp.c src/qp.h src/lu.h
	$(CC) $(CFLAGS) -I src -c -o $@ $<

tools/qpsolve.o: tools/qpsolve.c src/qp.h
	$(CC) $(CFLAGS) -I src -c -o $@ $<

%.o: %.c src/solver.h src/kernels.h src/lu.h src/splu.h src/parser.h src/qp.h
	$(CC) $(CFLAGS) -I src -c -o $@ $<

clean:
	rm -f lpsolve qpsolve src/*.o tools/*.o

.PHONY: all clean
