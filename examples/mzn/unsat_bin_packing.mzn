% Item weight exceeds all bin capacities (UNSAT)
include "bin_packing.mzn";
array[1..3] of var 1..2: bin :: output_array([1..3]);
array[1..3] of int: w = [6, 6, 6];
constraint bin_packing(5, bin, w); % max capacity 5, but each item has weight 6
solve satisfy;
