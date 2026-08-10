% Decreasing array constraint
include "decreasing.mzn";
array[1..4] of var 1..8: x :: output_array([1..4]);
constraint decreasing(x);
constraint x[1] = 7 /\ x[4] = 2;
solve maximize sum(x);
