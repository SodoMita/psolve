% Blending optimization LP
var 0.0..100.0: ing1 :: output_var;
var 0.0..100.0: ing2 :: output_var;
var 0.0..100.0: ing3 :: output_var;
% Total blend volume = 100
constraint ing1 + ing2 + ing3 = 100.0;
% Quality specs: Octane >= 90, Viscosity <= 40
constraint 85.0 * ing1 + 92.0 * ing2 + 98.0 * ing3 >= 9000.0;
constraint 50.0 * ing1 + 35.0 * ing2 + 25.0 * ing3 <= 4000.0;
solve minimize 30.0 * ing1 + 45.0 * ing2 + 60.0 * ing3;
