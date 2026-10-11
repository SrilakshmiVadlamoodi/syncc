#include "explain.h"

#include <stdlib.h>
#include <string.h>

#include "passes.h"
#include "tacutil.h"

static char **lines_of(const TACList *l) {
    char **v = malloc(sizeof(char *) * (l->count + 1));
    for (size_t i = 0; i < l->count; i++) v[i] = tac_instr_to_string(&l->items[i]);
    return v;
}

static void free_lines(char **v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

#define DP(i, j) dp[(i) * (m + 1) + (j)]

static void print_diff(const TACList *a, const TACList *b, FILE *out) {
    size_t n = a->count, m = b->count;
    if ((n + 1) * (m + 1) > 4000000u) {
        fprintf(out, "   (diff skipped: program too large)\n");
        return;
    }
    char **sa = lines_of(a);
    char **sb = lines_of(b);
    unsigned *dp = calloc((n + 1) * (m + 1), sizeof(unsigned));

    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            if (strcmp(sa[i], sb[j]) == 0) DP(i, j) = DP(i + 1, j + 1) + 1;
            else DP(i, j) = DP(i + 1, j) >= DP(i, j + 1) ? DP(i + 1, j) : DP(i, j + 1);
        }
    }

    size_t i = 0, j = 0;
    while (i < n && j < m) {
        if (strcmp(sa[i], sb[j]) == 0) { i++; j++; }
        else if (DP(i + 1, j) >= DP(i, j + 1)) fprintf(out, "   - %s\n", sa[i++]);
        else fprintf(out, "   + %s\n", sb[j++]);
    }
    while (i < n) fprintf(out, "   - %s\n", sa[i++]);
    while (j < m) fprintf(out, "   + %s\n", sb[j++]);

    free(dp);
    free_lines(sa, n);
    free_lines(sb, m);
}

void explain_sequence(TACList *input, const int *seq, int len,
                      const Env *initial, FILE *out) {
    size_t np = pass_count();
    long *saved = calloc(np ? np : 1, sizeof(long));
    TACList *cur = tac_copy_list(input);

    for (int s = 0; s < len; s++) {
        const PassInfo *p = &PASS_REGISTRY[seq[s]];
        TACList *next = p->fn(cur);
        long before = interp_dynamic_cost(cur, initial);
        long after = interp_dynamic_cost(next, initial);
        fprintf(out, "[%s] cost %ld -> %ld (%+ld)  instrs %zu -> %zu\n",
                p->name, before, after, after - before, cur->count, next->count);
        print_diff(cur, next, out);
        if (before >= 0 && after >= 0) saved[seq[s]] += before - after;
        taclist_free(cur);
        cur = next;
    }

    fprintf(out, "Saved per pass:\n");
    for (size_t p = 0; p < np; p++)
        if (saved[p] != 0)
            fprintf(out, "  %s saved %ld\n", PASS_REGISTRY[p].name, saved[p]);

    taclist_free(cur);
    free(saved);
}
