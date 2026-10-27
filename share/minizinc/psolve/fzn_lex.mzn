% Native lex / lex_chain / lex2 for psolve 1.6.0 — body-less, solver handles
% via exact MIP reifications (no big-M; success indicators are pinned by
% exact integer reifications).  Both enforced and reified (/imp) forms.
predicate fzn_lex_chain_lesseq_int(array[int,int] of var int: xs);
predicate fzn_lex_chain_lesseq_bool(array[int,int] of var bool: xs);
predicate fzn_lex_chain_less_int(array[int,int] of var int: xs);
predicate fzn_lex_chain_less_bool(array[int,int] of var bool: xs);
predicate fzn_lex2(array[int,int] of var int: xs);
predicate fzn_strict_lex2(array[int,int] of var int: xs);

% reified / half-reified 2D forms: b <-> lex condition
predicate fzn_lex_chain_lesseq_int_reif(array[int,int] of var int: xs, var bool: b);
predicate fzn_lex_chain_less_int_reif(array[int,int] of var int: xs, var bool: b);
predicate fzn_lex_chain_lesseq_int_imp(array[int,int] of var int: xs, var bool: b);
predicate fzn_lex_chain_less_int_imp(array[int,int] of var int: xs, var bool: b);
predicate fzn_lex2_reif(array[int,int] of var int: xs, var bool: b);
predicate fzn_strict_lex2_reif(array[int,int] of var int: xs, var bool: b);
predicate fzn_lex2_imp(array[int,int] of var int: xs, var bool: b);
predicate fzn_strict_lex2_imp(array[int,int] of var int: xs, var bool: b);

% reified / half-reified single 1D lex comparisons: c <-> lex_lesseq(xs,ys)
predicate fzn_lex_lesseq_int_reif(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_less_int_reif(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_lesseq_int_imp(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_less_int_imp(array[int] of var int: xs, array[int] of var int: ys, var bool: c);
predicate fzn_lex_lesseq_bool_reif(array[int] of var bool: xs, array[int] of var bool: ys, var bool: c);
predicate fzn_lex_less_bool_reif(array[int] of var bool: xs, array[int] of var bool: ys, var bool: c);
