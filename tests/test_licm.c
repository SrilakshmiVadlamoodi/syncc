#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "optimizer.h"
#include "tacutil.h"
#include "timing.h"

static char *licm_text(const char *in_text) {
    TACList *in = tac_from_text(in_text);
    TACList *out = optimize_loop_invariant_code_motion(in);
    char *s = tac_list_to_string(out);
    taclist_free(in);
    taclist_free(out);
    return s;
}

static void expect(const char *in, const char *want) {
    char *got = licm_text(in);
    if (strcmp(got, want) != 0) printf("  got:\n%s  wanted:\n%s", got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
}

#define LOOP_HEAD "i = 0\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n"
#define LOOP_TAIL "t9 = i + 1\ni = t9\ngoto L1\nL2:\n"


/* Behaviour is unchanged on a large input and it completes quickly. A body
 * of 15000 independent invariant temps must all be hoisted, in order, in
 * well under a second (the quadratic implementation took many seconds). */
static void test_large_loop(void) {
    enum { NTEMPS = 15000 };
    char *text = malloc((size_t)NTEMPS * 64 + 256);
    size_t len = 0;
    len += (size_t)sprintf(text + len, "i = 0\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n");
    for (int k = 0; k < NTEMPS; k++)
        len += (size_t)sprintf(text + len, "t%d = a * b\nx%d = t%d\n", 100 + k, k, 100 + k);
    len += (size_t)sprintf(text + len, "t9 = i + 1\ni = t9\ngoto L1\nL2:\n");

    TACList *in = tac_from_text(text);
    double t0 = now_seconds();
    TACList *out = optimize_loop_invariant_code_motion(in);
    double elapsed = now_seconds() - t0;

    CHECK(out->count == in->count);
    /* `i = 0`, then the NTEMPS hoisted temps, then the label */
    CHECK(out->items[1].op == TAC_BINOP);
    CHECK(strcmp(out->items[1].result, "t100") == 0);
    CHECK(strcmp(out->items[NTEMPS].result, "t15099") == 0);
    CHECK(out->items[NTEMPS + 1].op == TAC_LABEL);
    if (elapsed >= 1.0) printf("  LICM on %d temps took %.2fs\n", NTEMPS, elapsed);
    CHECK(elapsed < 1.0);
    taclist_free(in);
    taclist_free(out);
    free(text);
}
int main(void) {
    /* a user variable assigned in the body is never hoisted (zero-trip safety) */
    expect(LOOP_HEAD "x = a * b\n" LOOP_TAIL,
           LOOP_HEAD "x = a * b\n" LOOP_TAIL);

    /* an invariant temp is hoisted before the loop label */
    expect(LOOP_HEAD "t3 = a * b\nx = t3\n" LOOP_TAIL,
           "i = 0\nt3 = a * b\nL1:\nt1 = i < 3\nifFalse t1 goto L2\nx = t3\n" LOOP_TAIL);

    /* division by a variable is not hoisted (could trap on a zero-trip loop) */
    expect(LOOP_HEAD "t3 = a / b\nx = t3\n" LOOP_TAIL,
           LOOP_HEAD "t3 = a / b\nx = t3\n" LOOP_TAIL);

    /* division by a nonzero literal is hoisted */
    expect(LOOP_HEAD "t3 = a / 2\nx = t3\n" LOOP_TAIL,
           "i = 0\nt3 = a / 2\nL1:\nt1 = i < 3\nifFalse t1 goto L2\nx = t3\n" LOOP_TAIL);

    /* division by literal zero is not hoisted */
    expect(LOOP_HEAD "t3 = a / 0\nx = t3\n" LOOP_TAIL,
           LOOP_HEAD "t3 = a / 0\nx = t3\n" LOOP_TAIL);

    /* zero-trip loop with a hoistable user-variable assignment: x must stay 0 */
    {
        TACList *in = tac_from_text("i = 5\nL1:\nt1 = i < 3\nifFalse t1 goto L2\n"
                                    "x = a * b\nt9 = i + 1\ni = t9\ngoto L1\nL2:\n");
        TACList *out = optimize_loop_invariant_code_motion(in);
        Env *init = env_create();
        env_set(init, "a", value_int(2));
        env_set(init, "b", value_int(3));
        SymbolTable *sym = symtab_of("i x a b");
        InterpResult r0 = interp_run(in, init, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult r1 = interp_run(out, init, INTERP_DEFAULT_STEP_LIMIT);
        CHECK(interp_equivalent(&r0, &r1, sym));
        Value v;
        CHECK(env_get(r1.final_env, "x", &v) && v.i == 0);
        interp_result_free(&r0);
        interp_result_free(&r1);
        symtab_free(sym);
        env_free(init);
        taclist_free(in);
        taclist_free(out);
    }

    test_large_loop();
    TEST_END();
}
