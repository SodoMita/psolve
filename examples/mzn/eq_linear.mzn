
        var 0..20: x :: output_var;
        var 0..20: y :: output_var;
        constraint x + y = 15;
        constraint x - y = 3;
        solve satisfy;
    