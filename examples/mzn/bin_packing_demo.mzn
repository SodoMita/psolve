% 1D Bin Packing with uniform capacity
include "bin_packing.mzn";
array[1..4] of var 1..2: bin :: output_array([1..4]);
array[1..4] of int: w = [3, 2, 4, 3];
constraint bin_packing(6, bin, w);
solve satisfy;
