include "circuit.mzn";

array[1..4] of var 1..4: successor :: output_array([1..4]);
constraint circuit(successor);
solve satisfy;
