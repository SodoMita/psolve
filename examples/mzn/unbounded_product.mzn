% Unbounded monomial Diophantine equation:
%   x^3 * y^2 = 100000   with x,y unbounded integers.
% psolve's finite-domain CP engine factors the constant: x=10, y=+-10.
var int: x;
var int: y;
constraint x*x*x*y*y = 100000;
solve satisfy;
