#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "tacutil.h"

static void expect_pass(PassFn fn, const char *in_text, const char *want) {
    TACList *in = tac_from_text(in_text);
    TACList *out = fn(in);
    char *got = tac_list_to_string(out);
    if (strcmp(got, want) != 0) printf("  pass output:\n%s  wanted:\n%s", got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
    taclist_free(in);
    taclist_free(out);
}

static void test_registry(void) {
    CHECK(pass_index("fold") == 0);
    CHECK(pass_index("licm") == 1);
    CHECK(pass_index("copyprop") == 2);
    CHECK(pass_index("strength") == 3);
    CHECK(pass_index("nope") == -1);
    CHECK(PASS_REGISTRY[pass_count()].name == NULL);

    TACList *in = tac_from_text("x = 2 + 3\n");
    int seq[1] = { 0 };
    TACList *out = apply_sequence(in, seq, 1);
    char *s = tac_list_to_string(out);
    CHECK(strcmp(s, "x = 5\n") == 0);
    free(s);
    taclist_free(out);
    TACList *same = apply_sequence(in, NULL, 0);
    CHECK(same->count == 1 && same != in);
    taclist_free(same);
    taclist_free(in);
}

static void test_copyprop(void) {
    expect_pass(pass_copy_propagation,
                "x = 4\ny = x\nt1 = y * 2\n",
                "x = 4\ny = 4\nt1 = 4 * 2\n");
    /* source reassigned: the copy is no longer valid */
    expect_pass(pass_copy_propagation, "x = a\na = 5\ny = x\n", "x = a\na = 5\ny = x\n");
    /* state resets at labels */
    expect_pass(pass_copy_propagation, "x = 4\nL1:\ny = x\n", "x = 4\nL1:\ny = x\n");
    /* branch condition operand is rewritten */
    expect_pass(pass_copy_propagation, "c = t1\nifFalse c goto L1\nL1:\n",
                "c = t1\nifFalse t1 goto L1\nL1:\n");
    /* self-referential update does not create a bogus copy */
    expect_pass(pass_copy_propagation, "x = y\nx = x + 1\nz = x\n",
                "x = y\nx = y + 1\nz = x\n");
    /* empty program */
    expect_pass(pass_copy_propagation, "", "");
}

static void test_strength(void) {
    expect_pass(pass_strength_reduction, "t1 = x * 2\n", "t1 = x + x\n");
    expect_pass(pass_strength_reduction, "t1 = 2 * x\n", "t1 = x + x\n");
    expect_pass(pass_strength_reduction, "t1 = x * 1\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = 1 * x\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x + 0\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = 0 + x\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x - 0\n", "t1 = x\n");
    expect_pass(pass_strength_reduction, "t1 = x / 1\n", "t1 = x\n");
    /* never rewritten: x*0 (float inf/NaN), float literals, 0 - x */
    expect_pass(pass_strength_reduction, "t1 = x * 0\n", "t1 = x * 0\n");
    expect_pass(pass_strength_reduction, "t1 = x * 2.0\n", "t1 = x * 2.0\n");
    expect_pass(pass_strength_reduction, "t1 = 0 - x\n", "t1 = 0 - x\n");
}

/* Every registered pass must preserve semantics on every sample and never
 * increase dynamic cost on them. */
static void check_pass_on_source(const char *path) {
    char *src = pipeline_read_file(path);
    CHECK(src != NULL);
    if (!src) return;
    FrontEnd *fe = frontend_run(src);
    CHECK(fe->parse_errors == 0 && fe->semantic_errors == 0);
    InterpResult ref = interp_run(fe->tac, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(ref.status == INTERP_OK);
    for (size_t p = 0; p < pass_count(); p++) {
        TACList *out = PASS_REGISTRY[p].fn(fe->tac);
        InterpResult r = interp_run(out, NULL, INTERP_DEFAULT_STEP_LIMIT);
        int eq = interp_equivalent(&ref, &r, fe->symtab);
        if (!eq) printf("  pass %s broke %s\n", PASS_REGISTRY[p].name, path);
        CHECK(eq);
        CHECK(r.dynamic_cost <= ref.dynamic_cost);
        interp_result_free(&r);
        taclist_free(out);
    }
    interp_result_free(&ref);
    frontend_free(fe);
    free(src);
}

static void test_property_on_samples(void) {
    for (int i = 0; SAMPLE_FILES[i]; i++) check_pass_on_source(SAMPLE_FILES[i]);
}

int main(void) {
    test_registry();
    test_copyprop();
    test_strength();
    test_property_on_samples();
    TEST_END();
}
