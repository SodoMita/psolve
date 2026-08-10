% Graph 4-Coloring with symmetry breaking
include "alldifferent.mzn";
array[1..5] of var 1..4: color :: output_array([1..5]);
% Graph edges: (1,2), (1,3), (2,3), (2,4), (3,4), (3,5), (4,5)
constraint color[1] != color[2];
constraint color[1] != color[3];
constraint color[2] != color[3];
constraint color[2] != color[4];
constraint color[3] != color[4];
constraint color[3] != color[5];
constraint color[4] != color[5];
% Symmetry breaking: node 1 gets color 1, node 2 gets color 2
constraint color[1] = 1 /\ color[2] = 2;
solve satisfy;
