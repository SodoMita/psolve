% Increasing array constraint
include "increasing.mzn";
array[1..5] of var 1..10: x :: output_array([1..5]);
constraint increasing(x);
constraint x[1] >= 3 /\ x[5] <= 8;
solve maximize sum(x);
