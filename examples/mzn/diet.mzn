% Stigler nutrition diet LP
var 0.0..10.0: bread :: output_var;
var 0.0..10.0: milk :: output_var;
var 0.0..10.0: cheese :: output_var;
constraint 4.0 * bread + 8.0 * milk + 12.0 * cheese >= 20.0; % calories
constraint 2.0 * bread + 6.0 * milk + 1.0 * cheese >= 10.0;  % protein
constraint 1.0 * bread + 2.0 * milk + 5.0 * cheese >= 8.0;   % calcium
solve minimize 2.0 * bread + 3.5 * milk + 5.0 * cheese;
