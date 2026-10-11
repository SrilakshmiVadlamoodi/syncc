#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "heuristic.h"
#include "passes.h"
#include "tacutil.h"

#define CACHE_FILE "tests_tmp_cache.txt"

static void test_features(void) {
    TACList *empty = tac_from_text("");
    Features z = heuristic_features(empty);
    for (int i = 0; i < FEATURE_COUNT; i++) CHECK(z.v[i] == 0);
    taclist_free(empty);

    TACList *l = tac_from_text("i = 0\nL1:\nt1 = i < 5\nifFalse t1 goto L2\n"
                               "t2 = a * b\nt3 = 2 + 3\nx = t2\ngoto L1\nL2:\n");
    Features f = heuristic_features(l);
    CHECK(f.v[1] >= 1);   /* one loop (back-edge) */
    CHECK(f.v[2] >= 1);   /* one multiply */
    CHECK(f.v[4] >= 1);   /* one ifFalse */
    CHECK(f.v[5] >= 1);   /* one foldable binop */
    CHECK(f.v[6] >= 1);   /* one copy x = t2 */
    taclist_free(l);
}

static void test_learn_lookup(void) {
    HeuristicCache *c = cache_create();
    Features a = {{ 1, 0, 0, 0, 0, 0, 0 }};
    Features b = {{ 3, 1, 2, 0, 1, 0, 1 }};
    int s1[2] = { 2, 0 };
    int s2[1] = { 3 };
    cache_learn(c, &a, s1, 2, 0.8);
    cache_learn(c, &b, s2, 1, 0.5);

    int dist = -1;
    const CacheEntry *e = cache_lookup(c, &a, 8, &dist);
    CHECK(e != NULL && dist == 0 && e->len == 2 && e->seq[0] == 2);

    Features near_b = {{ 3, 1, 2, 0, 1, 0, 2 }};
    e = cache_lookup(c, &near_b, 8, &dist);
    CHECK(e != NULL && dist == 1 && e->len == 1 && e->seq[0] == 3);

    Features far = {{ 5, 5, 5, 5, 5, 5, 5 }};
    CHECK(cache_lookup(c, &far, 3, &dist) == NULL);

    /* same features: the lower ratio wins */
    int s3[1] = { 4 };
    cache_learn(c, &a, s3, 1, 0.9);   /* worse: ignored */
    e = cache_lookup(c, &a, 0, &dist);
    CHECK(e != NULL && e->len == 2);
    cache_learn(c, &a, s3, 1, 0.1);   /* better: replaces */
    e = cache_lookup(c, &a, 0, &dist);
    CHECK(e != NULL && e->len == 1 && e->seq[0] == 4);
    cache_free(c);
}

static void test_save_load_and_corruption(void) {
    HeuristicCache *c = cache_create();
    Features a = {{ 1, 0, 2, 0, 0, 0, 0 }};
    int s[3] = { pass_index("copyprop"), pass_index("fold"), pass_index("dce") };
    cache_learn(c, &a, s, 3, 0.25);
    CHECK(cache_save(c, CACHE_FILE));
    cache_free(c);

    /* append garbage lines: they must be skipped, valid ones kept */
    FILE *f = fopen(CACHE_FILE, "ab");
    fputs("this is not a cache line\n1 2 3\n9 9 9 9 9 9 9\tnosuchpass\t0.5\n", f);
    fclose(f);

    HeuristicCache *r = cache_load(CACHE_FILE);
    CHECK(r->count == 1);
    int dist;
    const CacheEntry *e = cache_lookup(r, &a, 0, &dist);
    CHECK(e != NULL && e->len == 3 && e->seq[2] == pass_index("dce"));
    CHECK(e->ratio > 0.24 && e->ratio < 0.26);
    cache_free(r);

    HeuristicCache *missing = cache_load("does_not_exist.cache");
    CHECK(missing != NULL && missing->count == 0);
    cache_free(missing);
    remove(CACHE_FILE);
}

static void test_leave_one_out_runs(void) {
    const char *files[] = {
        "samples/order_copyprop_fold.mini",
        "samples/order_strength_cse.mini",
        "samples/order_loop_mix.mini",
        "samples/loop_invariant.mini",
    };
    FILE *f = capture_open();
    int evaluated = heuristic_leave_one_out(files, 4, CACHE_DEFAULT_MAX_DISTANCE, f);
    char *text = capture_close(f);
    CHECK(evaluated == 4);
    CHECK(strstr(text, "hit rate") != NULL);
    free(text);
}

/* A cached/nearest sequence must never cost more than the fixed pipeline:
 * the compiler falls back to the baseline when the cached result is worse. */
static void test_cached_never_worse_than_fixed(void) {
    const char *files[] = {
        "samples/order_copyprop_fold.mini", "samples/order_strength_cse.mini",
        "samples/order_loop_mix.mini", "samples/loop_invariant.mini",
        "samples/loop_invariant_chain.mini", "samples/loop_no_invariant.mini",
        "samples/loop_with_if.mini", "samples/valid.mini", "samples/constant_folding.mini",
    };
    FILE *f = capture_open();
    int n = heuristic_leave_one_out(files, 9, CACHE_DEFAULT_MAX_DISTANCE, f);
    char *text = capture_close(f);
    CHECK(n == 9);

    int rows = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        long unopt, fixed, cached, searched;
        char path[128], how[32];
        if (sscanf(line, "%127s %31s %ld %ld %ld %ld", path, how, &unopt, &fixed, &cached, &searched) == 6) {
            rows++;
            if (cached > fixed) printf("  %s: cached %ld > fixed %ld\n", path, cached, fixed);
            CHECK(cached <= fixed);
            CHECK(searched <= cached);
        }
    }
    CHECK(rows == 9);
    free(text);
}

int main(void) {
    test_cached_never_worse_than_fixed();
    test_features();
    test_learn_lookup();
    test_save_load_and_corruption();
    test_leave_one_out_runs();
    TEST_END();
}
