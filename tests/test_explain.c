#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "explain.h"
#include "passes.h"
#include "tacutil.h"

int main(void) {
    TACList *in = tac_from_text("t1 = a * b\nt2 = a * b\nx = t1 + t2\n");
    int seq[1] = { pass_index("cse") };

    FILE *f = capture_open();
    explain_sequence(in, seq, 1, NULL, f);
    char *text = capture_close(f);

    CHECK(strstr(text, "[cse]") != NULL);
    CHECK(strstr(text, "cost 7 -> 5") != NULL);          /* 3+3+1 -> 3+1+1 */
    CHECK(strstr(text, "- t2 = a * b") != NULL);
    CHECK(strstr(text, "+ t2 = t1") != NULL);
    CHECK(strstr(text, "cse saved 2") != NULL);
    free(text);

    /* empty sequence and empty program must not crash */
    TACList *empty = tac_from_text("");
    f = capture_open();
    explain_sequence(empty, NULL, 0, NULL, f);
    free(capture_close(f));
    taclist_free(empty);

    taclist_free(in);
    TEST_END();
}
