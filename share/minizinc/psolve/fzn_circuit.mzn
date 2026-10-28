% psolve native circuit propagator (shadows std/fzn_circuit.mzn).
%
% The stdlib decomposition expands circuit(all_different + MTZ rank vars)
% into O(n^2) reified disequalities; for n=70 that is ~4800 constraints and
% a FlatZinc solver with an LP/MIP bridge cannot finish.  Declaring the
% body-less 1D builtin makes real mzn2fzn emit `fzn_circuit(xs)` directly,
% which src/fzn.c encodes with a compact O(n^2) assignment + MTZ model.
predicate fzn_circuit(array[int] of var int: xs);
