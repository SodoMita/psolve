% 3-Job 3-Machine Flow Shop Scheduling
include "disjunctive.mzn";
array[1..3, 1..3] of var 0..25: s :: output_array([1..3, 1..3]);
array[1..3, 1..3] of int: d = [|
    2, 3, 1 |
    3, 1, 2 |
    1, 2, 3 |];
var 0..25: makespan :: output_var;
% Precedence
constraint forall(j in 1..3)(
    s[j, 1] + d[j, 1] <= s[j, 2] /\
    s[j, 2] + d[j, 2] <= s[j, 3] /\
    s[j, 3] + d[j, 3] <= makespan
);
% Machine disjunctive
constraint forall(m in 1..3)(
    disjunctive([s[1, m], s[2, m], s[3, m]], [d[1, m], d[2, m], d[3, m]])
);
solve minimize makespan;
