% Infeasible Sudoku with conflicting clues (UNSAT)
include "alldifferent.mzn";
array[1..4, 1..4] of var 1..4: grid :: output_array([1..4, 1..4]);
constraint grid[1, 1] = 1;
constraint grid[1, 2] = 1; % direct row conflict
constraint forall(i in 1..4)(all_different([grid[i, j] | j in 1..4]));
solve satisfy;
