
        array[1..4] of int: arr = [10,20,30,40];
        var 1..4: idx :: output_var;
        var int: val :: output_var;
        constraint val = arr[idx];
        solve maximize val;
    