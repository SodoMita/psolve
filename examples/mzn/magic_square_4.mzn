% 4x4 Magic square
include "alldifferent.mzn";
array[1..4, 1..4] of var 1..16: s :: output_array([1..4, 1..4]);
constraint all_different([s[i, j] | i, j in 1..4]);
constraint forall(i in 1..4)(sum(j in 1..4)(s[i, j]) = 34);
constraint forall(j in 1..4)(sum(i in 1..4)(s[i, j]) = 34);
constraint s[1,1] + s[2,2] + s[3,3] + s[4,4] = 34;
constraint s[1,4] + s[2,3] + s[3,2] + s[4,1] = 34;
solve satisfy;
