% Strict lexicographical inequality
include "lex_less.mzn";
array[1..3] of var 1..3: x :: output_array([1..3]);
array[1..3] of var 1..3: y :: output_array([1..3]);
constraint x = [1, 2, 3];
constraint lex_less(x, y);
solve minimize sum(y);
