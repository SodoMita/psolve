% Enumerate all 6 permutations of 3 elements
include "alldifferent.mzn";
array[1..3] of var 1..3: p :: output_array([1..3]);
constraint all_different(p);
solve satisfy;
