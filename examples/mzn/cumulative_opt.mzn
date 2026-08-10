% Cumulative scheduling with makespan minimization
include "cumulative.mzn";
array[1..3] of var 0..12: s :: output_array([1..3]);
array[1..3] of int: d = [4, 3, 3];
array[1..3] of int: r = [1, 2, 1];
var 0..12: makespan :: output_var;
constraint cumulative(s, d, r, 2);
constraint forall(i in 1..3)(s[i] + d[i] <= makespan);
solve minimize makespan;
