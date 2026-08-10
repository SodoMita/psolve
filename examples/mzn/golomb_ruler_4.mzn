% 4-mark Golomb ruler optimization
include "alldifferent.mzn";
include "strictly_increasing.mzn";
array[1..4] of var 0..10: mark :: output_array([1..4]);
constraint mark[1] = 0;
constraint strictly_increasing(mark);
% Pairwise distances must all be distinct
constraint all_different([
    mark[2] - mark[1],
    mark[3] - mark[1],
    mark[4] - mark[1],
    mark[3] - mark[2],
    mark[4] - mark[2],
    mark[4] - mark[3]
]);
solve minimize mark[4];
