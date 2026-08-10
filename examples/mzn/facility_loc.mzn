% Uncapacitated Facility Location MIP
array[1..3] of var 0..1: open :: output_array([1..3]);
array[1..3, 1..4] of var 0..1: serve :: output_array([1..3, 1..4]);
array[1..3] of int: fixed_cost = [100, 120, 110];
array[1..3, 1..4] of int: trans_cost = [|
    10, 20, 30, 40 |
    25, 15, 20, 30 |
    30, 25, 15, 10 |];
% Each customer served by exactly one facility
constraint forall(j in 1..4)(sum(i in 1..3)(serve[i, j]) = 1);
% Cannot serve unless facility is open
constraint forall(i in 1..3, j in 1..4)(serve[i, j] <= open[i]);
solve minimize sum(i in 1..3)(fixed_cost[i] * open[i]) +
               sum(i in 1..3, j in 1..4)(trans_cost[i, j] * serve[i, j]);
