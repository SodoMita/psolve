% ---------------------------------------------------------------------------
% procstates_orbit_stock.mzn
%
% Same task as procstates_orbit.mzn (longest functional-graph orbit of the
% Unesty/Doing 16-bit automaton), but expressed ONLY with portable MiniZinc
% so stock solvers (Gecode, Chuffed, OR-Tools CP-SAT) can attempt it: a
% bounded-horizon unrolling with reified distinct-prefix indicators.
%
%   dst[k] = 1  <=>  x[0..k-1] are pairwise distinct
%
% len = sum(dst) then equals the orbit length PROVIDED the first repeated
% state of the walk occurs at an index <= H (H is data; the true optimum is
% 44, so H = 64 gives a sound window for this instance -- and knowing that
% number in advance is itself most of the problem).
%
% This is the encoding class where generic solvers grind: H chained
% array-element constraints over a 65536-entry table plus O(H^3) reified
% disequalities, with objective-driven search.  psolve does not need it: it
% implements orbit_len natively (see procstates_orbit.mzn), solving and
% PROVING the optimum in ~0.1 s.  Baselines: docs/PROCSTATES.md.
% ---------------------------------------------------------------------------

int: N;
array[0..N-1] of int: next;
int: H;

array[0..H] of var 0..N-1: x;
array[1..H] of var 0..1: dst;

var 0..N-1: start;
var 1..N:   len;

constraint x[0] = start;
constraint forall(i in 0..H-1) ( x[i+1] = next[x[i]] );

constraint dst[1] = 1;
constraint forall(k in 2..H) (
    dst[k] = bool2int(forall(i, j in 0..k-1 where i < j) (x[i] != x[j]))
);

constraint len = sum(k in 1..H) (dst[k]);

solve maximize len;

output ["len = ", show(len), "\nstart = ", show(start), "\n"];
