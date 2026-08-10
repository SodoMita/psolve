% AllEqual constraint
include "all_equal.mzn";
array[1..4] of var 1..10: x :: output_array([1..4]);
constraint all_equal(x);
constraint x[1] >= 6;
solve satisfy;
