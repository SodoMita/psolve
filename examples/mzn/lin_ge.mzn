
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint x - y >= 5;
        solve maximize x;
    