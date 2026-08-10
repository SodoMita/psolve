include "table.mzn";

array[1..2] of var 1..3: x :: output_array([1..2]);
array[1..3,1..2] of int: tuples =
  array2d(1..3, 1..2, [1,2, 2,3, 3,1]);

constraint table(x, tuples);
constraint x[1] >= 2;
solve maximize x[2];
