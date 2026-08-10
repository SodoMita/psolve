% 1D Cutting Stock MIP
% Cut demand: 3 of length 4, 2 of length 6, 2 of length 7 from rolls of length 15
array[1..5] of var 0..1: roll_used :: output_array([1..5]);
array[1..5, 1..3] of var 0..3: cut :: output_array([1..5, 1..3]);
constraint forall(r in 1..5)(
    4 * cut[r, 1] + 6 * cut[r, 2] + 7 * cut[r, 3] <= 15 * roll_used[r]
);
constraint sum(r in 1..5)(cut[r, 1]) >= 3;
constraint sum(r in 1..5)(cut[r, 2]) >= 2;
constraint sum(r in 1..5)(cut[r, 3]) >= 2;
solve minimize sum(r in 1..5)(roll_used[r]);
