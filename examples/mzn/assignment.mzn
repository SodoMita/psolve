% Linear Sum Assignment Problem (4 workers to 4 tasks)
include "alldifferent.mzn";
array[1..4] of var 1..4: assign :: output_array([1..4]);
array[1..4, 1..4] of int: cost = [|
    9, 2, 7, 8 |
    6, 4, 3, 7 |
    5, 8, 1, 8 |
    7, 6, 9, 4 |];
constraint all_different(assign);
solve minimize sum(w in 1..4)(cost[w, assign[w]]);
