% 2D Diffn non-overlapping rectangles
include "diffn.mzn";
array[1..2] of var 0..6: x :: output_array([1..2]);
array[1..2] of var 0..6: y :: output_array([1..2]);
array[1..2] of int: dx = [3, 2];
array[1..2] of int: dy = [2, 4];
constraint diffn(x, y, dx, dy);
constraint forall(i in 1..2)(x[i] + dx[i] <= 6 /\ y[i] + dy[i] <= 6);
solve satisfy;
