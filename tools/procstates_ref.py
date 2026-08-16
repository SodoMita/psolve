#!/usr/bin/env python3
"""Independent mirror of the 16-bit 'procstates' automaton transition
(Unesty/Doing attempts/procstates), written nibble-wise (a different style
from tools/procstates_ref.c on purpose) plus hand-computed unit states.
Verifies the C reference table over all 65536 states.

usage: procstates_ref.py NEXT_TXT      # full-table comparison only
       procstates_ref.py               # unit assertions only
"""
import sys

N = 65536

def xform(buf):
    st = [(buf >> (4 * k)) & 15 for k in range(4)]   # original cells (frozen)
    out = list(st)                                    # buf2
    # jumps[i]: loop counter; inert unless slots 0..i all 'loop'
    jumps = [0] * 4
    for i in range(4):
        ins, arg = st[i] & 3, st[i] >> 2
        if ins == 1:
            jc = arg + 1
            for ii in range(i - 1, -1, -1):
                jc *= jumps[ii]
            jumps[i] = jc
    putstate = 0      # 0 load, 1 store
    accum = 0
    j = 0
    exect = True
    while exect:
        for pc in range(4):
            ins, arg = st[pc] & 3, st[pc] >> 2
            if ins == 0:                    # put
                if putstate == 0:
                    accum = st[arg]
                    putstate = 1
                else:
                    putstate = 0
                    out[arg] = accum
            elif ins == 1:                  # loop: inert in body
                pass
            elif ins == 2:                  # inc, reads original
                out[arg] = (st[arg] + 1) & 15
            else:                           # dec
                out[arg] = (st[arg] - 1) & 15
        again = False
        while j < 4:
            if jumps[j] > 0:
                jumps[j] -= 1
                again = True
                break
            j += 1
        if not again:
            exect = False
    return out[0] | (out[1] << 4) | (out[2] << 8) | (out[3] << 12)


def units():
    # hand-computed expectations (derivation in commit message/docs)
    cases = {
        0x0000: 0x0000,   # 4 even puts, no-ops on zero
        2:      2,        # inc a0 then put-store clobbers it w/ original
        0xFFFF: 0xEFFF,   # 4x dec a3 -> single decrement (no accumulation)
        0xDDDD: 0xDDDD,   # all loop: 65813 inert passes
        29796:  29780,    # put load/store vs inc/dec ordering (0x7464->0x7454)
        33801:  32916,    # odd #puts with loop0(J0=3): pass-parity dynamics
        65314:  61219,    # inc twice same arg does NOT accumulate
    }
    for s, want in cases.items():
        got = xform(s)
        assert got == want, "xform(%d)=%d want %d" % (s, got, want)
    print("unit states: %d/%d OK" % (len(cases), len(cases)))


def main():
    units()
    if len(sys.argv) < 2:
        return 0
    ref = [int(x) for x in open(sys.argv[1]).read().split()]
    assert len(ref) == N, len(ref)
    bad = 0
    for s in range(N):
        if xform(s) != ref[s]:
            print("MISMATCH at %d: py=%d c=%d" % (s, xform(s), ref[s]))
            bad += 1
            if bad > 9:
                break
    if bad:
        print("mismatches:", bad)
        return 1
    print("full-table comparison: all %d states match" % N)
    return 0


if __name__ == "__main__":
    sys.exit(main())
