% Subcircuit constraint
include "subcircuit.mzn";
array[1..4] of var 1..4: next_node :: output_array([1..4]);
constraint subcircuit(next_node);
% Exclude the trivial all-self-loops solution
constraint next_node[1] != 1;
solve satisfy;
