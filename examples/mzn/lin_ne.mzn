
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint x + y != 7;
        solve maximize x;
    