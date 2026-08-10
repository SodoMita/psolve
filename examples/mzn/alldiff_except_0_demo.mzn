% AllDifferent except 0 constraint
include "alldifferent_except_0.mzn";
array[1..5] of var 0..4: x :: output_array([1..5]);
constraint alldifferent_except_0(x);
constraint x[1] = 0 /\ x[2] = 0 /\ x[3] = 1 /\ x[4] = 3;
solve maximize sum(x);
