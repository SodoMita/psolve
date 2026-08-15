#include "fzn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* FlatZinc-reader leak test (external audit F-3).
 *
 * Almost every fz_read() error path used to return -1 while leaving the
 * partially built FZModel (decls array, per-decl fields, constraint list,
 * m->file) allocated; command-line callers then exit without
 * fz_model_free(), and library callers were told the model was uninitialized.
 * (tools/fznsolve.c's `done:` path actually does call fz_model_free() even on
 * failure, which was safe only because failed reads leaked in silently
 * consistent shape.)  Every error path now frees the partial model.
 *
 * Why a C harness instead of the fuzzer: LeakSanitizer's root scan treats
 * pointers that still sit in dead stack frames as reachable, so leaks inside
 * a caller's just-returned frame are invisible when testing through the CLI.
 * This harness reads each input in a noinline helper and then overwrites the
 * stack region the frame lived in, so a partial model that survived a failed
 * fz_read() is genuinely unreachable and LeakSanitizer reports it.  It MUST
 * be run under an ASan/LSan build to be meaningful; the exit code only
 * reflects parse accept/reject, so always pair it with detect_leaks=1.
 *
 * Exits 0 when every case behaves as documented (the fail-cases are
 * structurally malformed and MUST be rejected; the accept-case must parse).
 */

__attribute__((noinline)) static int try_read_text(const char *text)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/fzleak_%d.fzn", (int)getpid());
    FILE *f = fopen(path, "w");
    if (!f) return -2;
    fputs(text, f);
    fclose(f);
    FZModel m;
    int rc = fz_read(path, &m);
    if (rc == 0) fz_model_free(&m);
    remove(path);
    return rc;
}

__attribute__((noinline)) static void clobber_stack(void)
{
    volatile char buf[32768];
    for (int i = 0; i < (int)sizeof buf; i++) buf[i] = (char)i;
}

int main(void)
{
    int fails = 0;
    struct { const char *text; int must_fail; } cases[] = {
        /* deep failure inside decl parsing AFTER several valid decls and a
           constraint: this used to leak the whole partial model */
        { "var 1..5: x :: output_var;\n"
          "var 1..5: y :: output_var;\n"
          "constraint int_le(x, y);\n"
          "var 7..3: z;\n"                    /* hi < lo: structural reject */
          "solve satisfy;\n", 1 },
        /* truncated range expression */
        { "var 1..5: x :: output_var;\n"
          "var 3..: q;\n"
          "solve satisfy;\n", 1 },
        /* unresolvable identifier domain: this reader cannot resolve names in
           type position, and silently swallowing it used to leave the
           declaration over the +-1e9 sentinel box (an interior optimum would
           not even trip the sentinel-hit UNKNOWN guard) -- must be rejected */
        { "var foo..bar: q;\n"
          "solve satisfy;\n", 1 },
        /* the legal-looking form of the same construct: a declared set
           parameter used as a domain (MiniZinc's compiler grounds domains to
           literals, so hand-written FZN like this is the only source) */
        { "var D: q;\n"
          "solve satisfy;\n", 1 },
        /* malformed array bound must be rejected, not leak */
        { "array [1..2] of var 1..3: xs = [x1, x2, x3];\n"
          "var 1..3: x1;\nvar 1..3: x2;\nvar 1..3: x3;\n"
          "constraint int_le(x1, x2);\n"
          "var 9..4: oops;\n"
          "solve satisfy;\n", 1 },
        /* well-formed control: must parse AND be freed cleanly */
        { "var 1..3: x :: output_var;\n"
          "constraint int_ge(x, 1);\n"
          "solve satisfy;\n", 0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int rc = try_read_text(cases[i].text);
        clobber_stack();
        if (cases[i].must_fail && rc == 0) {
            printf("fz_leak_test: case %zu should be rejected but parsed\n", i);
            fails++;
        } else if (!cases[i].must_fail && rc != 0) {
            printf("fz_leak_test: case %zu should parse but was rejected\n", i);
            fails++;
        }
    }
    /* the leak half of the verdict comes from LeakSanitizer at exit:
       run me with ASAN_OPTIONS=detect_leaks=1 */
    printf(fails ? "fz_leak_test: %d accept/reject FAILURES\n" : "fz_leak_test: accept/reject OK (leak verdict via LSan)\n", fails);
    return fails ? 1 : 0;
}
