
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        constraint 2*x + y <= 15;
        constraint x + 3*y <= 20;
        solve maximize x + y;
    