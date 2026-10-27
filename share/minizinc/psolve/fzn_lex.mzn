% Native 1D lexicographic comparisons (enforced / reified / half-reified).
predicate fzn_lex_lesseq_int(array[int] of var int: xs, array[int] of var int: ys);
predicate fzn_lex_less_int(array[int] of var int: xs, array[int] of var int: ys);
predicate fzn_lex_lesseq_bool(array[int] of var bool: xs, array[int] of var bool: ys);
predicate fzn_lex_less_bool(array[int] of var bool: xs, array[int] of var bool: ys);

predicate fzn_lex_lesseq_int_reif(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_less_int_reif(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_lesseq_int_imp(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_less_int_imp(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_lesseq_bool_reif(array[int] of var bool: xs, array[int] of var bool: ys, var bool: c);
predicate fzn_lex_less_bool_reif(array[int] of var bool: xs, array[int] of var bool: ys, var bool: c);
