
        include "alldifferent.mzn";
        array[1..3] of var 1..3: x :: output_array([1..3]);
        constraint all_different(x);
        solve satisfy;
    