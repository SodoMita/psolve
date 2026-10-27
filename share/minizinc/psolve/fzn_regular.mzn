% psolve native regular propagator (shadows std/fzn_regular.mzn).
%
% The stdlib's fzn_regular_reif.mzn checks `occurs(fzn_regular_is_decomposed)`.
% std/fzn_regular.mzn sets that flag to true; this file deliberately does NOT,
% so a reified regular constraint takes the stdlib augmented-DFA branch and
% posts a (non-reified) fzn_regular on an enlarged automaton -- which lands on
% this native propagator.  That is exactly how Gecode/Chuffed route regular.
%
% d is flattened row-major by array1d(); FlatZinc builtins may only take 1D
% arrays, so a 2D transition table cannot be passed to a native predicate.
predicate psolve_regular(array[int] of var int: x, int: Q, int: S,
                         array[int] of int: d, int: q0, set of int: F);
predicate fzn_regular(array[int] of var int: x, int: Q, int: S,
                      array[int,int] of int: d, int: q0, set of int: F) =
  psolve_regular(x, Q, S, array1d(d), q0, F);
