
        var -5..5: x :: output_var;
        var 0..5: y :: output_var;
        constraint y = abs(x);
        solve maximize x;
    