
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var bool: b :: output_var;
        constraint b <-> (x = y);
        constraint x <= 6;
        solve maximize x;
    