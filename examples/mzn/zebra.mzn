% Einstein Zebra logic puzzle (simplified 5 houses)
include "alldifferent.mzn";
var 1..5: English :: output_var;
var 1..5: Spaniard :: output_var;
var 1..5: Ukrainian :: output_var;
var 1..5: Norwegian :: output_var;
var 1..5: Japanese :: output_var;

var 1..5: Red :: output_var;
var 1..5: Green :: output_var;
var 1..5: Ivory :: output_var;
var 1..5: Yellow :: output_var;
var 1..5: Blue :: output_var;

constraint all_different([English, Spaniard, Ukrainian, Norwegian, Japanese]);
constraint all_different([Red, Green, Ivory, Yellow, Blue]);

% The Englishman lives in the red house
constraint English = Red;
% The green house is immediately to the right of the ivory house
constraint Green = Ivory + 1;
% The Norwegian lives in the first house
constraint Norwegian = 1;
% The Norwegian lives next to the blue house
constraint Norwegian = Blue + 1 \/ Norwegian = Blue - 1;
solve satisfy;
