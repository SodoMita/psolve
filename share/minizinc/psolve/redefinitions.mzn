% psolve native MiniZinc globals (v3.6).
%
% MiniZinc >= 2.8 rejects a body-less predicate whose FlatZinc builtin has a
% multi-dimensional array argument ("arrays must be one dimensional").  The
% v1.3-v1.7 mznlib declared fzn_lex2 / fzn_lex_chain_* / fzn_regular with 2D
% array arguments body-less, which made `minizinc --solver psolve MODEL.mzn`
% fail to compile *any* model under a modern MiniZinc.  This was never caught
% because the old test suite only fed hand-written .fzn to ./fznsolve.
%
% Fix: 2D globals are routed to 1D native FlatZinc builtins through per-file
% *redefinitions* (the same mechanism Gecode/Chuffed use).  MiniZinc resolves
% `include "fzn_X.mzn"` against the solver mznlib first, so a solver file with
% the same name as a stdlib decomposition file shadows it.  Each shadow file
% declares a legal 1D body-less builtin and defines the 2D fzn_* predicate as
% a thin adapter (array1d / explicit dimensions) around it.
include "fzn_value_precede_chain.mzn";
include "fzn_arg_min_max.mzn";
include "fzn_min_max.mzn";
