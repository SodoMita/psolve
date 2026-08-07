
        int: n = 4;
        array[1..n] of int: w = [2,3,4,5];
        array[1..n] of int: v = [3,4,5,6];
        array[1..n] of var 0..1: take :: output_array([1..n]);
        constraint sum(i in 1..n)(w[i]*take[i]) <= 8;
        solve maximize sum(i in 1..n)(v[i]*take[i]);
    