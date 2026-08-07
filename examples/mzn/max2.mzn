
        var 0..10: x :: output_var;
        var 0..10: y :: output_var;
        var 0..10: m :: output_var;
        constraint m = max(x,y);
        constraint x + y <= 12;
        solve maximize m;
    