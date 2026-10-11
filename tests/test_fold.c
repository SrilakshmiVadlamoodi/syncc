#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "interp.h"
#include "optimizer.h"
#include "tacutil.h"

static char *fold_text(const char *in_text) {
    TACList *in = tac_from_text(in_text);
    TACList *out = optimize_constant_folding(in);
    char *s = tac_list_to_string(out);
    taclist_free(in);
    taclist_free(out);
    return s;
}

static void expect(const char *in, const char *want) {
    char *got = fold_text(in);
    if (strcmp(got, want) != 0) printf("  folded %s-> %s(wanted %s)\n", in, got, want);
    CHECK(strcmp(got, want) == 0);
    free(got);
}

int main(void) {
    expect("t1 = 2 + 3\n", "t1 = 5\n");
    expect("t1 = 3 < 4\n", "t1 = 1\n");
    expect("t1 = 7 / 2\n", "t1 = 3\n");

    /* float results must stay float literals (contain '.') */
    expect("t1 = 2.0 * 2.0\n", "t1 = 4.0\n");
    expect("t1 = 1.5 + 2\n", "t1 = 3.5\n");

    /* not folded: divide by zero, overflow, results that need exponent form */
    expect("t1 = 1.0 / 0.0\n", "t1 = 1.0 / 0.0\n");
    expect("t1 = 5 / 0\n", "t1 = 5 / 0\n");
    expect("t1 = 9223372036854775807 + 1\n", "t1 = 9223372036854775807 + 1\n");
    expect("t1 = 99999999999999999999999999999999999999999.0 * 99999999999999999999999999999999999999999.0\n",
           "t1 = 99999999999999999999999999999999999999999.0 * 99999999999999999999999999999999999999999.0\n");

    /* precision: folded value must equal the runtime value exactly */
    {
        TACList *in = tac_from_text("t1 = 0.1 + 0.2\nx = t1\n");
        TACList *out = optimize_constant_folding(in);
        SymbolTable *sym = symtab_of("x");
        InterpResult a = interp_run(in, NULL, INTERP_DEFAULT_STEP_LIMIT);
        InterpResult b = interp_run(out, NULL, INTERP_DEFAULT_STEP_LIMIT);
        CHECK(interp_equivalent(&a, &b, sym));
        interp_result_free(&a);
        interp_result_free(&b);
        symtab_free(sym);
        taclist_free(in);
        taclist_free(out);
    }

    TEST_END();
}
