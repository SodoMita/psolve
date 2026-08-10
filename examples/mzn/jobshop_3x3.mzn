% 3-Job 3-Machine Job Shop Scheduling
include "disjunctive.mzn";
% s[j, m]: start time of job j on machine m
array[1..3, 1..3] of var 0..30: s :: output_array([1..3, 1..3]);
array[1..3, 1..3] of int: d = [|
    3, 2, 4 |
    2, 4, 3 |
    4, 3, 2 |];
var 0..30: makespan :: output_var;
% Precedence within each job: M1 -> M2 -> M3
constraint forall(j in 1..3)(
    s[j, 1] + d[j, 1] <= s[j, 2] /\
    s[j, 2] + d[j, 2] <= s[j, 3] /\
    s[j, 3] + d[j, 3] <= makespan
);
% No overlap on each machine across jobs
constraint forall(m in 1..3)(
    disjunctive([s[1, m], s[2, m], s[3, m]], [d[1, m], d[2, m], d[3, m]])
);
solve minimize makespan;
