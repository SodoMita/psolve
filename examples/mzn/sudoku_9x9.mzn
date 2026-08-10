% 9x9 Sudoku
include "alldifferent.mzn";
array[1..9, 1..9] of var 1..9: s :: output_array([1..9, 1..9]);
% Clues
constraint s[1,1]=5 /\ s[1,2]=3 /\ s[1,5]=7;
constraint s[2,1]=6 /\ s[2,4]=1 /\ s[2,5]=9 /\ s[2,6]=5;
constraint s[3,2]=9 /\ s[3,3]=8 /\ s[3,8]=6;
constraint s[4,1]=8 /\ s[4,5]=6 /\ s[4,9]=3;
constraint s[5,1]=4 /\ s[5,4]=8 /\ s[5,6]=3 /\ s[5,9]=1;
constraint s[6,1]=7 /\ s[6,5]=2 /\ s[6,9]=6;
constraint s[7,2]=6 /\ s[7,7]=2 /\ s[7,8]=8;
constraint s[8,4]=4 /\ s[8,5]=1 /\ s[8,6]=9 /\ s[8,9]=5;
constraint s[9,5]=8 /\ s[9,8]=7 /\ s[9,9]=9;
% Rows and columns
constraint forall(i in 1..9)(all_different([s[i, j] | j in 1..9]));
constraint forall(j in 1..9)(all_different([s[i, j] | i in 1..9]));
% 3x3 blocks
constraint forall(br in 0..2, bc in 0..2)(
    all_different([s[3*br + r, 3*bc + c] | r in 1..3, c in 1..3])
);
solve satisfy;
