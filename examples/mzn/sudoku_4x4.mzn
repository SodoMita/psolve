% 4x4 Mini-Sudoku
include "alldifferent.mzn";
array[1..4, 1..4] of var 1..4: grid :: output_array([1..4, 1..4]);
% Initial clues
constraint grid[1, 1] = 1;
constraint grid[2, 4] = 3;
constraint grid[3, 2] = 2;
constraint grid[4, 3] = 4;
% Rows and columns
constraint forall(i in 1..4)(all_different([grid[i, j] | j in 1..4]));
constraint forall(j in 1..4)(all_different([grid[i, j] | i in 1..4]));
% 2x2 blocks
constraint all_different([grid[1,1], grid[1,2], grid[2,1], grid[2,2]]);
constraint all_different([grid[1,3], grid[1,4], grid[2,3], grid[2,4]]);
constraint all_different([grid[3,1], grid[3,2], grid[4,1], grid[4,2]]);
constraint all_different([grid[3,3], grid[3,4], grid[4,3], grid[4,4]]);
solve satisfy;
