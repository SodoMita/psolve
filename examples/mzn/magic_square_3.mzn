% 3x3 Magic square
include "alldifferent.mzn";
array[1..3, 1..3] of var 1..9: s :: output_array([1..3, 1..3]);
constraint all_different([s[i, j] | i, j in 1..3]);
% Rows and columns sum to 15
constraint forall(i in 1..3)(sum(j in 1..3)(s[i, j]) = 15);
constraint forall(j in 1..3)(sum(i in 1..3)(s[i, j]) = 15);
% Diagonals sum to 15
constraint s[1,1] + s[2,2] + s[3,3] = 15;
constraint s[1,3] + s[2,2] + s[3,1] = 15;
solve satisfy;
