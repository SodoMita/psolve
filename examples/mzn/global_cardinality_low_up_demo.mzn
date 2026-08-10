% GCC with bounds
include "global_cardinality_low_up.mzn";
array[1..4] of var 1..3: x :: output_array([1..4]);
constraint global_cardinality_low_up(x, [1, 2, 3], [1, 1, 1], [2, 2, 2]);
solve satisfy;
