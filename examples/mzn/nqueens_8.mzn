% 8-Queens puzzle
include "alldifferent.mzn";
array[1..8] of var 1..8: q :: output_array([1..8]);
constraint all_different(q);
constraint all_different([q[i] + i | i in 1..8]);
constraint all_different([q[i] - i | i in 1..8]);
solve satisfy;
