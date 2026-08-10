% 4-Queens puzzle
include "alldifferent.mzn";
array[1..4] of var 1..4: q :: output_array([1..4]);
constraint all_different(q);
constraint all_different([q[i] + i | i in 1..4]);
constraint all_different([q[i] - i | i in 1..4]);
solve satisfy;
