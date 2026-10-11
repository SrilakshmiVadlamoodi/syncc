#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "fuzz.h"
#include "passes.h"
#include "pipeline.h"
#include "tacutil.h"

/* A deliberately wrong pass: turns every '+' into '-'. */
static TACList *broken_plus_to_minus(TACList *input) {
    TACList *out = tac_copy_list(input);
    for (size_t i = 0; i < out->count; i++)
        if (out->items[i].op == TAC_BINOP && out->items[i].binop == TOK_PLUS)
            out->items[i].binop = TOK_MINUS;
    return out;
}

static void test_generator_output_is_mostly_accepted(void) {
    int accepted = 0, total = 40;
    for (int i = 0; i < total; i++) {
        char *src = fuzz_generate_program(1000u + (unsigned)i, 6);
        FrontEnd *fe = frontend_run(src);
        if (fe->parse_errors == 0 && fe->semantic_errors == 0) accepted++;
        frontend_free(fe);
        free(src);
    }
    if (accepted * 10 < total * 9) printf("  only %d/%d generated programs accepted\n", accepted, total);
    CHECK(accepted * 10 >= total * 9);   /* at least 90% */
}

static void test_generator_is_deterministic(void) {
    char *a = fuzz_generate_program(7, 5);
    char *b = fuzz_generate_program(7, 5);
    char *c = fuzz_generate_program(8, 5);
    CHECK(strcmp(a, b) == 0);
    CHECK(strcmp(a, c) != 0);
    free(a); free(b); free(c);
}

static void test_correct_passes_survive_fuzzing(void) {
    FILE *f = capture_open();
    FuzzStats s = fuzz_run(60, 1, 4, NULL, f);
    char *log = capture_close(f);
    if (s.failures) printf("%s", log);
    CHECK(s.failures == 0);
    CHECK(s.checks > 200);
    CHECK(s.rejected * 10 <= s.programs);
    free(log);
}

static void test_fuzzer_catches_a_broken_pass(void) {
    PassInfo broken = { "broken", broken_plus_to_minus };
    FILE *f = capture_open();
    FuzzStats s = fuzz_run(40, 1, 3, &broken, f);
    char *log = capture_close(f);
    CHECK(s.failures > 0);
    CHECK(strstr(log, "broken") != NULL);
    free(log);
}

int main(void) {
    test_generator_output_is_mostly_accepted();
    test_generator_is_deterministic();
    test_correct_passes_survive_fuzzing();
    test_fuzzer_catches_a_broken_pass();
    TEST_END();
}
