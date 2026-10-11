#include "search.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "passes.h"
#include "tacutil.h"
#include "timing.h"

typedef struct {
    TACList *list;
    int seq[SEARCH_MAX_SEQ];
    int len;
} Node;

SearchConfig search_default_config(void) {
    return (SearchConfig){ .max_depth = 4, .budget = 2000 };
}

static uint64_t hash_list(const TACList *l) {
    char *s = tac_list_to_string(l);
    uint64_t h = 1469598103934665603ULL;
    for (const char *p = s; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
    free(s);
    return h;
}

static int seen_contains(const uint64_t *seen, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; i++)
        if (seen[i] == h) return 1;
    return 0;
}

/* Strictly better: lower cost, then fewer instructions, then shorter sequence. */
static int is_better(long cost, size_t ninstr, int seqlen,
                     long best_cost, size_t best_ninstr, int best_seqlen) {
    if (cost != best_cost) return cost < best_cost;
    if (ninstr != best_ninstr) return ninstr < best_ninstr;
    return seqlen < best_seqlen;
}

SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg, const Env *initial) {
    SearchResult res;
    memset(&res, 0, sizeof res);
    double t0 = now_seconds();

    res.best = tac_copy_list(input);
    res.input_cost = interp_dynamic_cost(input, initial);
    res.best_cost = res.input_cost;

    TACList *base = apply_fixed_pipeline(input);
    res.baseline_cost = interp_dynamic_cost(base, initial);
    taclist_free(base);

    if (res.input_cost < 0) {          /* cannot score: return the input unchanged */
        res.baseline_cost = res.input_cost;
        res.seconds = now_seconds() - t0;
        return res;
    }

    size_t npasses = pass_count();
    int max_depth = cfg->max_depth > SEARCH_MAX_SEQ ? SEARCH_MAX_SEQ : cfg->max_depth;

    uint64_t *seen = malloc(sizeof(uint64_t) * ((size_t)cfg->budget + 2));
    size_t nseen = 0;
    seen[nseen++] = hash_list(input);

    Node *frontier = malloc(sizeof(Node));
    frontier[0].list = tac_copy_list(input);
    frontier[0].len = 0;
    size_t nf = 1;

    int stop = 0;
    for (int depth = 1; depth <= max_depth && nf > 0 && !stop; depth++) {
        Node *next = malloc(sizeof(Node) * nf * npasses);
        size_t nn = 0;

        for (size_t f = 0; f < nf && !stop; f++) {
            for (size_t p = 0; p < npasses; p++) {
                if (res.candidates >= cfg->budget) { stop = 1; break; }

                TACList *cand = PASS_REGISTRY[p].fn(frontier[f].list);
                uint64_t h = hash_list(cand);
                if (seen_contains(seen, nseen, h)) { taclist_free(cand); continue; }
                seen[nseen++] = h;
                res.candidates++;

                long cost = interp_dynamic_cost(cand, initial);
                if (cost < 0) {            /* a sound pass never does this */
                    res.invalid_candidates++;
                    taclist_free(cand);
                    continue;
                }

                Node n;
                n.list = cand;
                n.len = frontier[f].len + 1;
                memcpy(n.seq, frontier[f].seq, sizeof(int) * (size_t)frontier[f].len);
                n.seq[frontier[f].len] = (int)p;

                if (is_better(cost, cand->count, n.len,
                              res.best_cost, res.best->count, res.sequence_len)) {
                    taclist_free(res.best);
                    res.best = tac_copy_list(cand);
                    res.best_cost = cost;
                    res.sequence_len = n.len;
                    memcpy(res.sequence, n.seq, sizeof(int) * (size_t)n.len);
                }
                next[nn++] = n;
            }
        }

        for (size_t f = 0; f < nf; f++) taclist_free(frontier[f].list);
        free(frontier);
        frontier = next;
        nf = nn;
    }

    for (size_t f = 0; f < nf; f++) taclist_free(frontier[f].list);
    free(frontier);
    free(seen);

    res.seconds = now_seconds() - t0;
    return res;
}

void search_result_free(SearchResult *r) {
    taclist_free(r->best);
    r->best = NULL;
}

void search_print_sequence(const SearchResult *r, FILE *out) {
    if (r->sequence_len == 0) {
        fprintf(out, "(none)");
        return;
    }
    for (int i = 0; i < r->sequence_len; i++)
        fprintf(out, "%s%s", i ? ", " : "", PASS_REGISTRY[r->sequence[i]].name);
}
