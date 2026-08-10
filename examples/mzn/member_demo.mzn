% Member constraint
include "member.mzn";
array[1..4] of var 1..10: x :: output_array([1..4]);
var 1..10: target :: output_var;
constraint x = [2, 5, 7, 9];
constraint member(x, target);
constraint target >= 6;
solve minimize target;
