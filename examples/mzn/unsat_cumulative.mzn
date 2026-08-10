% Over-subscribed cumulative resource (UNSAT)
include "cumulative.mzn";
array[1..2] of var 0..2: s :: output_array([1..2]);
array[1..2] of int: d = [3, 3];
array[1..2] of int: r = [2, 2];
constraint cumulative(s, d, r, 2); % capacity 2, each task needs 2 and must overlap
constraint s[1] = 0 /\ s[2] = 0;
solve satisfy;
