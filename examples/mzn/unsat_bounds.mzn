% Incompatible variable bounds (UNSAT)
var 1..3: x :: output_var;
var 5..8: y :: output_var;
constraint x = y;
solve satisfy;
