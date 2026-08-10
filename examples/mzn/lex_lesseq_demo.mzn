% Lexicographical inequality
include "lex_lesseq.mzn";
array[1..3] of var 1..3: x :: output_array([1..3]);
array[1..3] of var 1..3: y :: output_array([1..3]);
constraint x = [1, 2, 3];
constraint lex_lesseq(x, y);
solve minimize sum(y);
