% Cryptarithmetic SEND + MORE = MONEY
include "alldifferent.mzn";
var 1..9: S :: output_var;
var 0..9: E :: output_var;
var 0..9: N :: output_var;
var 0..9: D :: output_var;
var 1..9: M :: output_var;
var 0..9: O :: output_var;
var 0..9: R :: output_var;
var 0..9: Y :: output_var;
constraint all_different([S, E, N, D, M, O, R, Y]);
constraint                1000*S + 100*E + 10*N + D
           +              1000*M + 100*O + 10*R + E
           = 10000*M + 1000*O + 100*N + 10*E + Y;
solve satisfy;
