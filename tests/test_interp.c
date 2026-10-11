#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"

static InterpResult run(const char *text) {
    TACList *l = tac_from_text(text);
    InterpResult r = interp_run(l, NULL, INTERP_DEFAULT_STEP_LIMIT);
    taclist_free(l);
    return r;
}

static void test_straight_line(void) {
    InterpResult r = run("x = 5\ny = x * 3\n");
    CHECK(r.status == INTERP_OK);
    Value v;
    CHECK(env_get(r.final_env, "y", &v) && v.kind == VAL_INT && v.i == 15);
    CHECK(r.dynamic_cost == 4);   /* assign 1 + mul 3 */
    CHECK(r.steps == 2);
    interp_result_free(&r);
}

static void test_loop_cost_and_steps(void) {
    InterpResult r = run("x = 3\nL1:\nt1 = x > 0\nifFalse t1 goto L2\n"
                         "t2 = x - 1\nx = t2\ngoto L1\nL2:\n");
    CHECK(r.status == INTERP_OK);
    Value v;
    CHECK(env_get(r.final_env, "x", &v) && v.i == 0);
    CHECK(r.dynamic_cost == 22);
    CHECK(r.steps == 23);
    interp_result_free(&r);
}

static void test_float_and_int_division(void) {
    InterpResult r = run("a = 1.5\nb = a + 2\nc = 7\nd = c / 2\n");
    Value v;
    CHECK(env_get(r.final_env, "b", &v) && v.kind == VAL_FLOAT && v.f == 3.5);
    CHECK(env_get(r.final_env, "d", &v) && v.kind == VAL_INT && v.i == 3);
    interp_result_free(&r);
}

static void test_traps_and_limits(void) {
    InterpResult r = run("a = 0\nb = 1 / a\n");
    CHECK(r.status == INTERP_DIV_ZERO);
    interp_result_free(&r);

    TACList *l = tac_from_text("L1:\ngoto L1\n");
    InterpResult s = interp_run(l, NULL, 100);
    CHECK(s.status == INTERP_STEP_LIMIT);
    CHECK(s.steps == 100);
    interp_result_free(&s);
    CHECK(interp_dynamic_cost(l, NULL) == -1);
    taclist_free(l);
}

static void test_initial_env_and_passthrough(void) {
    Env *init = env_create();
    env_set(init, "x", value_int(41));
    env_set(init, "unused", value_int(9));
    TACList *l = tac_from_text("y = x + 1\n");
    InterpResult r = interp_run(l, init, INTERP_DEFAULT_STEP_LIMIT);
    Value v;
    CHECK(env_get(r.final_env, "y", &v) && v.i == 42);
    CHECK(env_get(r.final_env, "x", &v) && v.i == 41);
    CHECK(env_get(r.final_env, "unused", &v) && v.i == 9);   /* untouched names survive */
    interp_result_free(&r);
    taclist_free(l);
    env_free(init);
}

static void test_equivalence(void) {
    SymbolTable *sym = symtab_of("x y");
    InterpResult a = run("t1 = 2 + 3\nx = t1\ny = 1\n");
    InterpResult b = run("x = 5\ny = 1\n");
    InterpResult c = run("x = 5\ny = 2\n");
    InterpResult d = run("x = 5.0\ny = 1\n");   /* same number, different kind */
    CHECK(interp_equivalent(&a, &b, sym));       /* temps ignored */
    CHECK(!interp_equivalent(&a, &c, sym));
    CHECK(!interp_equivalent(&a, &d, sym));
    interp_result_free(&a); interp_result_free(&b);
    interp_result_free(&c); interp_result_free(&d);
    symtab_free(sym);
}

static void test_empty_program(void) {
    TACList *l = tac_from_text("");
    InterpResult r = interp_run(l, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(r.status == INTERP_OK);
    CHECK(r.dynamic_cost == 0 && r.steps == 0);
    interp_result_free(&r);
    CHECK(interp_dynamic_cost(l, NULL) == 0);
    taclist_free(l);
}

int main(void) {
    test_straight_line();
    test_loop_cost_and_steps();
    test_float_and_int_division();
    test_traps_and_limits();
    test_initial_env_and_passthrough();
    test_equivalence();
    test_empty_program();
    TEST_END();
}
