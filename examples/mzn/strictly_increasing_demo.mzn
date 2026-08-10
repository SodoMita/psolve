% Strictly increasing array constraint
include "strictly_increasing.mzn";
array[1..4] of var 1..10: x :: output_array([1..4]);
constraint strictly_increasing(x);
constraint x[1] = 2;
solve maximize sum(x);
