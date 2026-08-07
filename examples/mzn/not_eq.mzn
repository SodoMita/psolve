
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var bool: b :: output_var;
        constraint b <-> (x = y);
        constraint not b;
        solve maximize x;
    