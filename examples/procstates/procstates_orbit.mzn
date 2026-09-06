% ---------------------------------------------------------------------------
% procstates_orbit.mzn
%
% MiniZinc model of Unesty/Doing attempts/procstates: a u16 "computer" state
% is four nibbles; nibble k doubles as instruction k (opcode:u2, arg:u2 with
% opcodes 0=put / 1=loop / 2=inc / 3=dec) and as memory cell v_k:u4.  One
% step of the machine maps a state to a state via execute():
%   - all memory READS see the ORIGINAL state, all writes go to a copy;
%   - a 'loop' opcode only repeats whole 4-slot passes, and its counter
%     multiplies the counters of ALL preceding slots (so a loop is inert
%     unless every earlier slot is also a loop);
%   - 'put' toggles a load/store flip-flop that persists ACROSS the repeated
%     passes (load: accum = v[arg]; store: v2[arg] = accum).
% The transition is therefore a pure function on 65536 states; the puzzle
% (main.zig's longestPath) is the functional-graph question:
%
%     find a start state with the LONGEST orbit until the first repeat
%     (tail length + cycle length).
%
% Ground truth (verified, tools/procstates_ref.c cross-checked against the
% independent mirror tools/procstates_ref.py on all 65536 states, plus
% hand-derived unit states): max orbit 44, e.g. start 51641 (tail 42 into
% a 2-cycle at 15679).  The next[] table is compiled data produced by that
% verified reference -- the tiny VM is not re-encoded stepwise here, because
% unrolling a data-dependent number of VM passes (up to 65813!) as CP
% constraints is precisely the formulation where generic solvers die.
%
% Decision problem as a solver global (psolve implements it natively):
%     orbit_len(next, start, len)  <=>  len = #distinct states visited by
%     the walk start, next[start], next^2[start], ... before its first
%     repeated state, i.e. tail(start) + cycle length of its component.
% ---------------------------------------------------------------------------

int: N;                                    % number of states (data)
array[0..N-1] of int: next;                % transition table (data)

% declaration-only: psolve provides this as a builtin global constraint.
% (See procstates_orbit_stock.mzn for a portable bounded definition usable
% with stock solvers, and docs/PROCSTATES.md for the measured baselines.)
predicate orbit_len(array[int] of int: nxt, var int: start, var int: len);

var 0..N-1: start;
var 1..N:   len;

constraint orbit_len(next, start, len);

solve maximize len;

output ["orbit_len = ", show(len), "\nstart = ", show(start), "\n"];
