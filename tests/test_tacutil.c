#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "optimizer.h"
#include "pipeline.h"
#include "strmap.h"
#include "tacutil.h"

static void test_predicates(void) {
    CHECK(tac_is_numeric_literal("5"));
    CHECK(tac_is_numeric_literal("-3"));
    CHECK(tac_is_numeric_literal("2.5"));
    CHECK(!tac_is_numeric_literal("t1"));
    CHECK(!tac_is_numeric_literal("-"));
    CHECK(!tac_is_numeric_literal(""));
    CHECK(!tac_is_numeric_literal(NULL));
    CHECK(tac_is_float_literal("2.0"));
    CHECK(!tac_is_float_literal("2"));
    CHECK(tac_is_temp_name("t1"));
    CHECK(tac_is_temp_name("t42"));
    CHECK(!tac_is_temp_name("t"));
    CHECK(!tac_is_temp_name("total"));
    CHECK(!tac_is_temp_name("x1"));
}

static void test_strmap(void) {
    StrMap *m = strmap_create();
    CHECK(strmap_find(m, "a") == -1);
    CHECK(strmap_intern(m, "a") == 0);
    CHECK(strmap_intern(m, "b") == 1);
    CHECK(strmap_intern(m, "a") == 0);
    CHECK(strmap_find(m, "b") == 1);
    CHECK(strmap_count(m) == 2);
    CHECK(strcmp(strmap_key(m, 1), "b") == 0);
    for (int i = 0; i < 1000; i++) {   /* force several rehashes */
        char k[16];
        snprintf(k, sizeof k, "k%d", i);
        strmap_intern(m, k);
    }
    CHECK(strmap_count(m) == 1002);
    CHECK(strmap_find(m, "k999") == 1001);
    strmap_free(m);
}

static void test_text_round_trip(void) {
    const char *text = "x = 5\nt1 = x * 3\nL1:\nifFalse t1 goto L2\ngoto L1\nL2:\n";
    TACList *list = tac_from_text(text);
    CHECK(list->count == 6);
    char *back = tac_list_to_string(list);
    CHECK(strcmp(back, text) == 0);
    free(back);
    taclist_free(list);
}

static void test_fold_baseline(void) {
    TACList *in = tac_from_text("t1 = 2 + 3\nx = t1\n");
    TACList *out = optimize_constant_folding(in);
    char *s = tac_list_to_string(out);
    CHECK(strcmp(s, "t1 = 5\nx = t1\n") == 0);
    free(s);
    taclist_free(in);
    taclist_free(out);
}

static void test_frontend(void) {
    FrontEnd *fe = frontend_run("int x; x = 1 + 2;");
    CHECK(fe->parse_errors == 0);
    CHECK(fe->semantic_errors == 0);
    CHECK(fe->tac->count == 2);
    CHECK(!frontend_has_temp_collision(fe->symtab));
    frontend_free(fe);

    FrontEnd *bad = frontend_run("int t1; t1 = 2;");
    CHECK(frontend_has_temp_collision(bad->symtab));
    frontend_free(bad);
}

int main(void) {
    test_predicates();
    test_strmap();
    test_text_round_trip();
    test_fold_baseline();
    test_frontend();
    TEST_END();
}
