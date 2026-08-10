% 3-Job 3-Machine Open Shop Scheduling
include "disjunctive.mzn";
array[1..3, 1..3] of var 0..20: s :: output_array([1..3, 1..3]);
array[1..3, 1..3] of int: d = [|
    2, 1, 3 |
    1, 3, 2 |
    3, 2, 1 |];
var 0..20: makespan :: output_var;
% Machine non-overlap
constraint forall(m in 1..3)(
    disjunctive([s[1, m], s[2, m], s[3, m]], [d[1, m], d[2, m], d[3, m]])
);
% Job non-overlap (each job on at most one machine at a time)
constraint forall(j in 1..3)(
    disjunctive([s[j, 1], s[j, 2], s[j, 3]], [d[j, 1], d[j, 2], d[j, 3]])
);
constraint forall(j in 1..3, m in 1..3)(s[j, m] + d[j, m] <= makespan);
solve minimize makespan;
