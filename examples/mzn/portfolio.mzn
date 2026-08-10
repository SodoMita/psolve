% Bounded linear portfolio allocation
var 0.0..1.0: x1 :: output_var;
var 0.0..1.0: x2 :: output_var;
var 0.0..1.0: x3 :: output_var;
var 0.0..1.0: x4 :: output_var;
constraint x1 + x2 + x3 + x4 = 1.0;
% Risk limits
constraint 0.10 * x1 + 0.15 * x2 + 0.25 * x3 + 0.05 * x4 <= 0.12;
% Diversification: max 40% in any asset
constraint x1 <= 0.4 /\ x2 <= 0.4 /\ x3 <= 0.4 /\ x4 <= 0.4;
solve maximize 0.08 * x1 + 0.12 * x2 + 0.18 * x3 + 0.04 * x4;
