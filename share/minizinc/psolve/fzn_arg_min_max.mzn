% Native arg_min/arg_max for psolve 1.3.0 — body-less, solver handles via MIP (non-reif)
predicate fzn_minimum_arg_int(array[int] of var int: x, var int: i);
predicate fzn_minimum_arg_bool(array[int] of var bool: x, var int: i);
predicate fzn_maximum_arg_int(array[int] of var int: x, var int: i);
predicate fzn_maximum_arg_bool(array[int] of var bool: x, var int: i);
predicate fzn_minimum_arg_int_reif(array[int] of var int: x, var int: i, var bool: b);
predicate fzn_maximum_arg_int_reif(array[int] of var int: x, var int: i, var bool: b);
