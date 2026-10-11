#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "search.h"
#include "tacutil.h"

static void test_beats_fixed_pipeline(void) {
    TACList *in = tac_from_text("x = 4\ny = x\nt1 = y * 2\nz = t1\n");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(in, &cfg, NULL);

    CHECK(r.input_cost == 6);
    CHECK(r.baseline_cost == 6);          /* fold + licm find nothing */
    CHECK(r.best_cost <= 3);              /* copyprop, fold, copyprop, dce */
    CHECK(r.best_cost < r.baseline_cost);
    CHECK(r.sequence_len >= 1);
    CHECK(r.candidates > 0 && r.candidates <= cfg.budget);

    SymbolTable *sym = symtab_of("x y z");
    InterpResult a = interp_run(in, NULL, INTERP_DEFAULT_STEP_LIMIT);
    InterpResult b = interp_run(r.best, NULL, INTERP_DEFAULT_STEP_LIMIT);
    CHECK(interp_equivalent(&a, &b, sym));
    interp_result_free(&a);
    interp_result_free(&b);
    symtab_free(sym);

    /* replaying the reported sequence reproduces `best` exactly */
    TACList *again = apply_sequence(in, r.sequence, r.sequence_len);
    char *s1 = tac_list_to_string(again);
    char *s2 = tac_list_to_string(r.best);
    CHECK(strcmp(s1, s2) == 0);
    free(s1);
    free(s2);
    taclist_free(again);

    search_result_free(&r);
    taclist_free(in);
}

static void test_never_worse_than_baseline(void) {
    const char *programs[] = {
        "i = 0\ns = 0\nL1:\nt1 = i < 5\nifFalse t1 goto L2\nt2 = s + i\ns = t2\nt3 = i + 1\ni = t3\ngoto L1\nL2:\n",
        "a = 2 + 3\nb = a * 1\n",
        "x = y\n",
        NULL
    };
    for (int i = 0; programs[i]; i++) {
        TACList *in = tac_from_text(programs[i]);
        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(in, &cfg, NULL);
        CHECK(r.best_cost <= r.baseline_cost);
        CHECK(r.best_cost <= r.input_cost);
        search_result_free(&r);
        taclist_free(in);
    }
}

static void test_budget_is_respected(void) {
    TACList *in = tac_from_text("x = 4\ny = x\nt1 = y * 2\nz = t1\n");
    SearchConfig cfg = { 4, 3 };
    SearchResult r = search_best_sequence(in, &cfg, NULL);
    CHECK(r.candidates <= 3);
    search_result_free(&r);
    taclist_free(in);
}

static void test_unscoreable_input_returned_unchanged(void) {
    TACList *loop = tac_from_text("L1:\ngoto L1\n");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(loop, &cfg, NULL);
    CHECK(r.sequence_len == 0);
    CHECK(r.candidates == 0);
    char *s = tac_list_to_string(r.best);
    CHECK(strcmp(s, "L1:\ngoto L1\n") == 0);
    free(s);
    search_result_free(&r);
    taclist_free(loop);

    TACList *trap = tac_from_text("a = 0\nb = 1 / a\n");
    SearchResult t = search_best_sequence(trap, &cfg, NULL);
    CHECK(t.sequence_len == 0 && t.candidates == 0);
    search_result_free(&t);
    taclist_free(trap);
}

static void test_empty_program(void) {
    TACList *in = tac_from_text("");
    SearchConfig cfg = search_default_config();
    SearchResult r = search_best_sequence(in, &cfg, NULL);
    CHECK(r.best != NULL && r.best->count == 0);
    CHECK(r.best_cost == 0);
    search_result_free(&r);
    taclist_free(in);
}

int main(void) {
    test_beats_fixed_pipeline();
    test_never_worse_than_baseline();
    test_budget_is_respected();
    test_unscoreable_input_returned_unchanged();
    test_empty_program();
    TEST_END();
}
