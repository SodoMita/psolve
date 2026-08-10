% 4x4 Latin Square
include "alldifferent.mzn";
array[1..4, 1..4] of var 1..4: square :: output_array([1..4, 1..4]);
constraint forall(i in 1..4)(all_different([square[i, j] | j in 1..4]));
constraint forall(j in 1..4)(all_different([square[i, j] | i in 1..4]));
constraint square[1, 1] = 1 /\ square[1, 2] = 2 /\ square[1, 3] = 3 /\ square[1, 4] = 4;
solve satisfy;
