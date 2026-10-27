% Native minimum/maximum (generic) for psolve 1.3.0 — body-less, solver handles via selector MIP
predicate fzn_minimum_int(var int: m, array[int] of var int: xs);
predicate fzn_maximum_int(var int: m, array[int] of var int: xs);
predicate fzn_minimum_bool(var bool: m, array[int] of var bool: xs);
predicate fzn_maximum_bool(var bool: m, array[int] of var bool: xs);
