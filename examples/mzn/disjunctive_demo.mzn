% 1D Disjunctive task scheduling
include "disjunctive.mzn";
array[1..3] of var 0..15: s :: output_array([1..3]);
array[1..3] of int: d = [3, 4, 2];
constraint disjunctive(s, d);
constraint forall(i in 1..3)(s[i] + d[i] <= 15);
solve minimize max(i in 1..3)(s[i] + d[i]);
