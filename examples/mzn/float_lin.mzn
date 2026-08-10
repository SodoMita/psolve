% Bounded continuous float LP
var 0.0..10.0: x :: output_var;
var 0.0..10.0: y :: output_var;
constraint 2.5 * x + 1.5 * y <= 15.0;
constraint 1.0 * x + 3.0 * y <= 18.0;
solve maximize 3.0 * x + 4.0 * y;
