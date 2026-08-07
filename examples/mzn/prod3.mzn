
        var 0..20: a :: output_var;
        var 0..20: b :: output_var;
        var 0..20: c :: output_var;
        constraint a + b + c <= 25;
        constraint 2*a + b <= 30;
        constraint b + 3*c <= 40;
        solve maximize a + 2*b + 3*c;
    