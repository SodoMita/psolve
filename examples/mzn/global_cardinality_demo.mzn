% Global cardinality constraint (GCC)
include "global_cardinality.mzn";
array[1..5] of var 1..3: x :: output_array([1..5]);
array[1..3] of var 0..5: counts :: output_array([1..3]);
constraint global_cardinality(x, [1, 2, 3], counts);
constraint counts = [2, 2, 1];
solve satisfy;
