% Bridge construction precedence network
array[1..6] of var 0..40: s :: output_array([1..6]);
array[1..6] of int: d = [4, 6, 8, 5, 3, 2];
var 0..40: duration :: output_var;
% Precedence
constraint s[1] + d[1] <= s[2];
constraint s[1] + d[1] <= s[3];
constraint s[2] + d[2] <= s[4];
constraint s[3] + d[3] <= s[4];
constraint s[4] + d[4] <= s[5];
constraint s[5] + d[5] <= s[6];
constraint s[6] + d[6] <= duration;
solve minimize duration;
