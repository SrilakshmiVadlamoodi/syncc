#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"

#ifdef _WIN32
#define MINICC ".\\minicc.exe"
#else
#define MINICC "./minicc"
#endif

static void test_every_sample_is_clean_and_search_is_sound(void) {
    for (int i = 0; SAMPLE_FILES[i]; i++) {
        char *src = pipeline_read_file(SAMPLE_FILES[i]);
        CHECK(src != NULL);
        if (!src) continue;
        FrontEnd *fe = frontend_run(src);
        CHECK(fe->parse_errors == 0 && fe->semantic_errors == 0);

        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        InterpResult a = interp_run(fe->tac, NULL, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult b = interp_run(r.best, NULL, INTERP_DEFAULT_STEP_LIMIT);
        int eq = interp_equivalent(&a, &b, fe->symtab);
        if (!eq) printf("  search result not equivalent on %s\n", SAMPLE_FILES[i]);
        CHECK(eq);
        CHECK(r.best_cost <= r.baseline_cost);
        CHECK(r.invalid_candidates == 0);
        interp_result_free(&a);
        interp_result_free(&b);
        search_result_free(&r);
        frontend_free(fe);
        free(src);
    }
}

static void test_ordering_sensitive_samples_improve(void) {
    const char *files[] = {
        "samples/order_copyprop_fold.mini",
        "samples/order_strength_cse.mini",
        "samples/order_loop_mix.mini",
        NULL
    };
    for (int i = 0; files[i]; i++) {
        char *src = pipeline_read_file(files[i]);
        CHECK(src != NULL);
        if (!src) continue;
        FrontEnd *fe = frontend_run(src);
        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        if (r.best_cost >= r.baseline_cost)
            printf("  %s: searched %ld vs fixed %ld\n", files[i], r.best_cost, r.baseline_cost);
        CHECK(r.best_cost < r.baseline_cost);
        search_result_free(&r);
        frontend_free(fe);
        free(src);
    }
}

static void test_temp_name_collision_detected(void) {
    FrontEnd *fe = frontend_run("int t1; t1 = 2 * 3;");
    CHECK(frontend_has_temp_collision(fe->symtab));
    frontend_free(fe);
}

/* End to end through the real driver. */
static char *run_cli(const char *args) {
    char cmd[512];
    snprintf(cmd, sizeof cmd, MINICC " %s > tests_tmp_cli.out", args);
    system(cmd);
    char *text = pipeline_read_file("tests_tmp_cli.out");
    remove("tests_tmp_cli.out");
    return text ? text : tac_dup_str("");
}

static void test_cli_search_and_stats(void) {
    char *out = run_cli("--search --stats samples/order_loop_mix.mini");
    CHECK(strstr(out, "STAT unoptimized") != NULL);
    CHECK(strstr(out, "STAT fixed") != NULL);
    CHECK(strstr(out, "STAT searched") != NULL);
    CHECK(strstr(out, "VERIFY searched ok") != NULL);
    CHECK(strstr(out, "MISMATCH") == NULL);
    free(out);

    char *plain = run_cli("samples/valid.mini");
    CHECK(strstr(plain, "=== Tokens ===") != NULL);
    CHECK(strstr(plain, "STAT") == NULL);   /* new sections appear only with flags */
    free(plain);

    char *timed = run_cli("--time samples/valid.mini");
    CHECK(strstr(timed, "TIME lex") != NULL);
    free(timed);
}


static void test_cli_use_cache_explain_shows_cached_sequence(void) {
    remove("tests_tmp_cache2.txt");
    char *learned = run_cli("--learn --cache tests_tmp_cache2.txt samples/order_strength_cse.mini");
    free(learned);
    char *out = run_cli("--use-cache --explain --cache tests_tmp_cache2.txt samples/order_strength_cse.mini");
    remove("tests_tmp_cache2.txt");
    CHECK(strstr(out, "=== Explain (cached) ===") != NULL);
    /* the cached sequence is strength, cse, copyprop, dce -- not the fixed fold, licm */
    CHECK(strstr(out, "[strength]") != NULL);
    CHECK(strstr(out, "[cse]") != NULL);
    free(out);
}
int main(void) {
    test_every_sample_is_clean_and_search_is_sound();
    test_ordering_sensitive_samples_improve();
    test_temp_name_collision_detected();
    test_cli_search_and_stats();
    test_cli_use_cache_explain_shows_cached_sequence();
    TEST_END();
}
