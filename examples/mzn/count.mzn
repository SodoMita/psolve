
        array[1..4] of var 1..3: x :: output_array([1..4]);
        constraint count(x, 2) = 2;
        solve satisfy;
    