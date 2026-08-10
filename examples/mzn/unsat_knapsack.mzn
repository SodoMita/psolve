% Overconstrained Knapsack (UNSAT)
array[1..3] of int: w = [4, 5, 6];
array[1..3] of int: v = [2, 3, 4];
array[1..3] of var 0..1: take :: output_array([1..3]);
constraint sum(i in 1..3)(w[i] * take[i]) <= 5;
constraint sum(i in 1..3)(v[i] * take[i]) >= 10;
solve satisfy;
