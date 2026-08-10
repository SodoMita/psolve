% Number of distinct values
include "nvalue.mzn";
array[1..5] of var 1..4: x :: output_array([1..5]);
var 1..5: nv :: output_var;
constraint nvalue(nv, x);
constraint x[1] = 1 /\ x[2] = 2 /\ x[3] = 2 /\ x[4] = 3 /\ x[5] = 1;
solve satisfy;
