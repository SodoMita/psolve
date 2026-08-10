% 4 pigeons in 3 holes (UNSAT)
include "alldifferent.mzn";
array[1..4] of var 1..3: pigeon :: output_array([1..4]);
constraint all_different(pigeon);
solve satisfy;
