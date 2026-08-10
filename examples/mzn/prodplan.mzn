% Production planning multi-period model
var 0..50: p1 :: output_var;
var 0..50: p2 :: output_var;
var 0..50: p3 :: output_var;
constraint p1 + p2 + p3 <= 60;
constraint 2 * p1 + p2 <= 50;
constraint p2 + 3 * p3 <= 70;
solve maximize 5 * p1 + 4 * p2 + 6 * p3;
