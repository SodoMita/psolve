/* Reference implementation of the "procstates" 16-bit automaton
 * (Unesty/Doing attempts/procstates): a u16 state is 4 nibbles.  Read as
 * ProcessorState, nibble k = (i_k:u2, a_k:u2) = instruction k (0=put,
 * 1=loop, 2=inc, 3=dec) with 2-bit memory argument; read as ProcessorMem,
 * nibble k = v_k:u4, a memory cell.  IMPORTANT (mirrors the Zig): all
 * memory READS inside execute() are from the ORIGINAL input buf, all writes
 * go to buf2; instructions are fetched from the frozen input; `loop` only
 * repeats whole 4-slot passes; its counter multiplies the counters of ALL
 * preceding slots (so a loop is inert unless every earlier slot is also a
 * loop); the pass boundary scan index j never resets, hence total passes =
 * 1 + sum(jumps).  `put` toggles a global load/store flip-flop: load sets
 * accum = v[arg], store writes v2[arg] = accum; the flip-flop persists
 * across passes within one execute() call and is reset to load afterwards.
 *
 * This program computes next[] for all 65536 states, then the functional
 * graph: cycle/tail decomposition and the maximum-orbit witness
 * (the quantity main.zig's longestPath() computes as bestpathlen).
 * Prints a line-oriented report; with --emit-next PATH also writes one
 * decimal next-state per line (65536 lines) for model data generation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define N 65536

static uint16_t xform(uint16_t buf) {
    uint16_t buf2 = buf;
    int jumps[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        unsigned ins = (unsigned)(buf >> (4 * i)) & 3u;
        unsigned arg = (unsigned)(buf >> (4 * i + 2)) & 3u;
        if (ins == 1u) {
            int jc = (int)arg + 1;          /* 0 loops are useless */
            for (int ii = i - 1; ii >= 0; ii--) jc *= jumps[ii];
            jumps[i] = jc;
        }
    }
    int putstate = 0;                       /* 0 = load, 1 = store */
    unsigned accum = 0;
    int j = 0, exect = 1;
    while (exect) {
        for (int pc = 0; pc < 4; pc++) {
            unsigned ins = (unsigned)(buf >> (4 * pc)) & 3u;
            unsigned arg = (unsigned)(buf >> (4 * pc + 2)) & 3u;
            switch (ins) {
            case 0u:                        /* put */
                if (putstate == 0) { accum = (unsigned)(buf >> (4 * arg)) & 15u; putstate = 1; }
                else { putstate = 0;
                       buf2 = (uint16_t)((buf2 & ~(0xFu << (4 * arg))) | ((uint16_t)(accum & 15u) << (4 * arg))); }
                break;
            case 1u: break;                 /* loop: inert in the body */
            case 2u: {                      /* inc, reads ORIGINAL buf */
                unsigned v = ((unsigned)(buf >> (4 * arg)) & 15u);
                v = (v + 1u) & 15u;
                buf2 = (uint16_t)((buf2 & ~(0xFu << (4 * arg))) | ((uint16_t)v << (4 * arg)));
                break; }
            default: {                      /* dec */
                unsigned v = ((unsigned)(buf >> (4 * arg)) & 15u);
                v = (v + 15u) & 15u;
                buf2 = (uint16_t)((buf2 & ~(0xFu << (4 * arg))) | ((uint16_t)v << (4 * arg)));
                break; }
            }
        }
        int again = 0;
        while (j < 4) {
            if (jumps[j] > 0) { jumps[j] -= 1; again = 1; break; }
            else j += 1;
        }
        if (!again) exect = 0;
    }
    return buf2;
}

int main(int argc, char **argv) {
    static unsigned char nxt2[N];           /* low byte */
    static unsigned char nxt2b[N];          /* high byte */
    static int next_[N];
    for (int s = 0; s < N; s++) {
        uint16_t t = xform((uint16_t)s);
        next_[s] = t; nxt2[s] = (unsigned char)(t & 255); nxt2b[s] = (unsigned char)(t >> 8);
    }
    (void)nxt2; (void)nxt2b;

    /* functional graph decomposition: depth (orbit length to first repeat),
       via per-walk marking, memoized across states */
    static int depth[N];                    /* 0 = unknown */
    static int seen[N];                     /* walk id that visited s, 0 = none */
    static int pos[N];                      /* position within that walk */
    static int order[N];
    int maxorb = -1, argmax = -1;
    long ncomp = 0;
    static int cyc_hist[N + 1];
    for (int s = 0; s < N; s++) {
        if (depth[s]) continue;
        ncomp++;
        int k = 0, cur = s;
        while (1) {
            if (depth[cur]) {               /* hit known territory */
                int i;
                for (i = k - 1; i >= 0; i--)
                    depth[order[i]] = depth[cur] + (k - i);
                break;
            }
            if (seen[cur] == s + 1) {       /* hit own walk: cycle */
                int p = pos[cur], i;
                int cyc_len = k - p;
                cyc_hist[cyc_len]++;
                for (i = p; i < k; i++) depth[order[i]] = cyc_len;
                for (i = p - 1; i >= 0; i--)
                    depth[order[i]] = depth[order[i + 1]] + 1;
                break;
            }
            seen[cur] = s + 1; pos[cur] = k; order[k] = cur; k++;
            cur = next_[cur];
        }
    }
    for (int s = 0; s < N; s++)
        if (depth[s] > maxorb) { maxorb = depth[s]; argmax = s; }

    printf("states=%d components=%ld max_orbit=%d argmax=%d\n", N, ncomp, maxorb, argmax);
    /* argmax decomposition: tail length, cycle length, cycle entry */
    {
        int cur = argmax, k = 0;
        static int seen2[N];
        while (1) {
            if (seen2[cur]) break;
            seen2[cur] = k + 1; order[k] = cur; k++; cur = next_[cur];
        }
        int p = seen2[cur] - 1;
        printf("witness_start=%d tail=%d cycle=%d cycle_entry=%d orbit=%d\n",
               argmax, p, k - p, cur, k);
        printf("cycles:");
        for (int i = 1; i <= N; i++) if (cyc_hist[i]) printf(" len%d=x%d", i, cyc_hist[i]);
        printf("\n");
    }
    /* fixed points & short cycles summary */
    {
        long fixed = 0, self2 = 0;
        for (int s = 0; s < N; s++) {
            if (next_[s] == s) fixed++;
            else if (next_[next_[s]] == s) self2++;
        }
        printf("fixed_points=%ld two_cycle_nodes=%ld\n", fixed, self2);
    }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--emit-next") && i + 1 < argc) {
            FILE *f = fopen(argv[i + 1], "w");
            if (!f) { perror("fopen"); return 1; }
            for (int s = 0; s < N; s++) fprintf(f, "%d\n", next_[s]);
            fclose(f);
            fprintf(stderr, "wrote %s\n", argv[i + 1]);
        }
    return 0;
}
