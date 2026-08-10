% Unbounded integer knapsack problem
int: capacity = 15;
array[1..3] of int: weights = [3, 4, 5];
array[1..3] of int: values = [4, 5, 7];
array[1..3] of var 0..5: take :: output_array([1..3]);
constraint sum(i in 1..3)(weights[i] * take[i]) <= capacity;
solve maximize sum(i in 1..3)(values[i] * take[i]);
