CC      ?= gcc
OPT     ?= -O3
ARCH    ?= -march=native
CFLAGS   = -std=gnu11 -Wall -Wextra -O3 $(ARCH) -mavx512f -mfma -funroll-loops \
           -fno-math-errno -ffast-math
LDLIBS   = -lm

SRC = src/kernels.c src/lu.c src/splu.c src/solver.c src/parser.c src/main.c
OBJ = $(SRC:.c=.o)

all: lpsolve

lpsolve: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c src/solver.h src/kernels.h src/lu.h src/parser.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f lpsolve src/*.o

.PHONY: all clean
