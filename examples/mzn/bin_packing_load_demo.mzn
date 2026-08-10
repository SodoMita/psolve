% Bin Packing with load variables
include "bin_packing_load.mzn";
array[1..2] of var 0..10: load :: output_array([1..2]);
array[1..4] of var 1..2: bin :: output_array([1..4]);
array[1..4] of int: w = [2, 3, 4, 1];
constraint bin_packing_load(load, bin, w);
constraint load[1] <= 5 /\ load[2] <= 5;
solve satisfy;
