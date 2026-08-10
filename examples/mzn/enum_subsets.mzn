% Enumerate all binary subsets with sum <= 2
array[1..3] of var 0..1: take :: output_array([1..3]);
constraint sum(take) <= 2;
solve satisfy;
