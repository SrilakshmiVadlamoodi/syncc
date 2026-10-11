#include <stdlib.h>
#include <string.h>

#include "support.h"
#include "cost.h"

static int cost_of(const char *line) {
    TACList *l = tac_from_text(line);
    int c = cost_of_instr(&l->items[0]);
    taclist_free(l);
    return c;
}

int main(void) {
    CHECK(cost_of("x = 5\n") == 1);
    CHECK(cost_of("t = a + b\n") == 1);
    CHECK(cost_of("t = a - b\n") == 1);
    CHECK(cost_of("t = a < b\n") == 1);
    CHECK(cost_of("t = a == b\n") == 1);
    CHECK(cost_of("t = a * b\n") == 3);
    CHECK(cost_of("t = a / b\n") == 10);
    CHECK(cost_of("ifFalse t goto L1\n") == 2);
    CHECK(cost_of("goto L1\n") == 1);
    CHECK(cost_of("L1:\n") == 0);

    TACList *l = tac_from_text("x = 1\nL1:\nt = x * 2\ngoto L1\n");
    CHECK(cost_static_ops(l) == 3);   /* labels are not counted */
    taclist_free(l);

    TACList *empty = tac_from_text("");
    CHECK(cost_static_ops(empty) == 0);
    taclist_free(empty);

    TEST_END();
}
