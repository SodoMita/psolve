% Sliding sum moving window
include "sliding_sum.mzn";
array[1..6] of var 1..5: x :: output_array([1..6]);
constraint sliding_sum(4, 7, 2, x);
solve maximize sum(x);
