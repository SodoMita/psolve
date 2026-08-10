% Inverse permutation channeling
include "inverse.mzn";
array[1..4] of var 1..4: f :: output_array([1..4]);
array[1..4] of var 1..4: invf :: output_array([1..4]);
constraint inverse(f, invf);
constraint f[1] = 3 /\ f[2] = 4 /\ f[3] = 1 /\ f[4] = 2;
solve satisfy;
