% Cumulative renewable resource scheduling
include "cumulative.mzn";
array[1..4] of var 0..10: s :: output_array([1..4]);
array[1..4] of int: d = [3, 2, 2, 2];
array[1..4] of int: r = [1, 2, 1, 1];
constraint cumulative(s, d, r, 2);
constraint forall(i in 1..4)(s[i] + d[i] <= 8);
solve satisfy;
