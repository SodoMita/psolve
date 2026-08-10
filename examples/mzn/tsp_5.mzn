% 5-City Traveling Salesperson via circuit
include "circuit.mzn";
array[1..5] of var 1..5: next_city :: output_array([1..5]);
array[1..5, 1..5] of int: dist = [|
    0, 10, 8, 9, 7 |
    10, 0, 10, 5, 6 |
    8, 10, 0, 8, 9 |
    9, 5, 8, 0, 6 |
    7, 6, 9, 6, 0 |];
constraint circuit(next_city);
solve minimize sum(i in 1..5)(dist[i, next_city[i]]);
