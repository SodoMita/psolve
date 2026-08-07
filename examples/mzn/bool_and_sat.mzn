
        var bool: a :: output_var;
        var bool: b :: output_var;
        var bool: c :: output_var;
        constraint (a /\ b) = c;
        constraint a \/ b;
        solve satisfy;
    