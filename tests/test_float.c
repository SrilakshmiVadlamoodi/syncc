#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"

/* Final value of `name` after compiling `src` and running the TAC chosen by
 * `mode`: 0 = unoptimized, 1 = fixed pipeline, 2 = searched. */
static Value final_value(const char *src, const char *name, int mode) {
    FrontEnd *fe = frontend_run(src);
    CHECK(fe->parse_errors == 0 && fe->semantic_errors == 0);
    TACList *tac = tac_copy_list(fe->tac);
    if (mode == 1) {
        TACList *t = apply_fixed_pipeline(fe->tac);
        taclist_free(tac);
        tac = t;
    } else if (mode == 2) {
        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        taclist_free(tac);
        tac = tac_copy_list(r.best);
        search_result_free(&r);
    }
    InterpResult r = interp_run(tac, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(r.status == INTERP_OK);
    Value v = value_int(-999);
    CHECK(env_get(r.final_env, name, &v));
    interp_result_free(&r);
    taclist_free(tac);
    frontend_free(fe);
    return v;
}

static void check_float(const char *src, const char *name, double want) {
    for (int mode = 0; mode < 3; mode++) {
        Value v = final_value(src, name, mode);
        if (v.kind != VAL_FLOAT || v.f != want)
            printf("  mode %d: %s = %s%g (wanted float %g)\n", mode, name,
                   v.kind == VAL_FLOAT ? "" : "int ", v.kind == VAL_FLOAT ? v.f : (double)v.i, want);
        CHECK(v.kind == VAL_FLOAT);
        CHECK(v.f == want);
    }
}

static void test_widening_assignment_keeps_float_semantics(void) {
    /* int literal widened into a float variable, then divided: must be 0.5, not 0 */
    check_float("float h; h = 1; h = h / 2;", "h", 0.5);
    /* int variable widened into a float variable */
    check_float("float q; int i; i = 3; q = i;", "q", 3.0);
    /* int division happens first (C semantics), then the result is widened */
    check_float("float f; f = 7 / 2;", "f", 3.0);
}

static void test_integral_float_literals_stay_float(void) {
    check_float("float g; g = 3.0; g = g / 2.0;", "g", 1.5);
    check_float("float f; f = 7.0 / 2.0;", "f", 3.5);
}

static void test_float_literals_keep_precision_and_large_values(void) {
    check_float("float p; p = 0.1234567891;", "p", 0.1234567891);
    check_float("float k; k = 100000000.0 + 0.5;", "k", 100000000.5);
    check_float("float b; b = 100000000.0;", "b", 100000000.0);
}

int main(void) {
    test_widening_assignment_keeps_float_semantics();
    test_integral_float_literals_stay_float();
    test_float_literals_keep_precision_and_large_values();
    TEST_END();
}
