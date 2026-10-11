#include "heuristic.h"

#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "strmap.h"
#include "tacutil.h"

static int bucket(long x) {
    if (x <= 0) return 0;
    if (x <= 2) return 1;
    if (x <= 5) return 2;
    if (x <= 10) return 3;
    if (x <= 20) return 4;
    return 5;
}

Features heuristic_features(const TACList *list) {
    long n_instr = 0, loops = 0, mul = 0, div = 0, branch = 0, foldable = 0, copies = 0;
    StrMap *labels = strmap_create();
    for (size_t i = 0; i < list->count; i++) {
        const TACInstr *in = &list->items[i];
        if (in->op == TAC_LABEL) { strmap_intern(labels, in->result); continue; }
        n_instr++;
        if (in->op == TAC_GOTO && strmap_find(labels, in->result) >= 0) loops++;  /* back-edge */
        if (in->op == TAC_IF_FALSE_GOTO) branch++;
        if (in->op == TAC_BINOP) {
            if (in->binop == TOK_STAR) mul++;
            if (in->binop == TOK_SLASH) div++;
            if (tac_is_numeric_literal(in->arg1) && tac_is_numeric_literal(in->arg2)) foldable++;
        }
        if (in->op == TAC_ASSIGN && !tac_is_numeric_literal(in->arg1)) copies++;
    }
    strmap_free(labels);

    Features f;
    f.v[0] = bucket(n_instr);
    f.v[1] = bucket(loops);
    f.v[2] = bucket(mul);
    f.v[3] = bucket(div);
    f.v[4] = bucket(branch);
    f.v[5] = bucket(foldable);
    f.v[6] = bucket(copies);
    return f;
}

int feature_distance(const Features *a, const Features *b) {
    int d = 0;
    for (int i = 0; i < FEATURE_COUNT; i++) d += abs(a->v[i] - b->v[i]);
    return d;
}

HeuristicCache *cache_create(void) {
    return calloc(1, sizeof(HeuristicCache));
}

void cache_free(HeuristicCache *c) {
    if (!c) return;
    free(c->items);
    free(c);
}

static void cache_push(HeuristicCache *c, CacheEntry e) {
    if (c->count == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->items = realloc(c->items, sizeof(CacheEntry) * c->cap);
    }
    c->items[c->count++] = e;
}

void cache_learn(HeuristicCache *c, const Features *f, const int *seq, int len, double ratio) {
    CacheEntry e;
    memset(&e, 0, sizeof e);
    e.f = *f;
    e.len = len > SEARCH_MAX_SEQ ? SEARCH_MAX_SEQ : len;
    if (e.len > 0) memcpy(e.seq, seq, sizeof(int) * (size_t)e.len);
    e.ratio = ratio;

    for (size_t i = 0; i < c->count; i++) {
        if (feature_distance(&c->items[i].f, f) == 0) {
            if (ratio < c->items[i].ratio) c->items[i] = e;
            return;
        }
    }
    cache_push(c, e);
}

const CacheEntry *cache_lookup(const HeuristicCache *c, const Features *f,
                               int max_distance, int *distance_out) {
    const CacheEntry *best = NULL;
    int best_d = 0;
    for (size_t i = 0; i < c->count; i++) {
        int d = feature_distance(&c->items[i].f, f);
        if (d > max_distance) continue;
        if (!best || d < best_d || (d == best_d && c->items[i].ratio < best->ratio)) {
            best = &c->items[i];
            best_d = d;
        }
    }
    if (best && distance_out) *distance_out = best_d;
    return best;
}

int cache_save(const HeuristicCache *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    for (size_t i = 0; i < c->count; i++) {
        const CacheEntry *e = &c->items[i];
        for (int k = 0; k < FEATURE_COUNT; k++)
            fprintf(f, "%d%s", e->f.v[k], k + 1 < FEATURE_COUNT ? " " : "");
        fputc('\t', f);
        if (e->len == 0) fputc('-', f);
        for (int k = 0; k < e->len; k++)
            fprintf(f, "%s%s", k ? "," : "", PASS_REGISTRY[e->seq[k]].name);
        fprintf(f, "\t%.4f\n", e->ratio);
    }
    fclose(f);
    return 1;
}

HeuristicCache *cache_load(const char *path) {
    HeuristicCache *c = cache_create();
    FILE *f = fopen(path, "rb");
    if (!f) return c;

    char line[1024];
    while (fgets(line, sizeof line, f)) {
        Features ft;
        char seqtxt[512];
        double ratio;
        int n = sscanf(line, "%d %d %d %d %d %d %d\t%511[^\t]\t%lf",
                       &ft.v[0], &ft.v[1], &ft.v[2], &ft.v[3], &ft.v[4], &ft.v[5], &ft.v[6],
                       seqtxt, &ratio);
        if (n != 9) continue;

        int seq[SEARCH_MAX_SEQ], len = 0, ok = 1;
        if (strcmp(seqtxt, "-") != 0) {
            for (char *tok = strtok(seqtxt, ","); tok; tok = strtok(NULL, ",")) {
                int idx = pass_index(tok);
                if (idx < 0 || len >= SEARCH_MAX_SEQ) { ok = 0; break; }
                seq[len++] = idx;
            }
        }
        if (!ok) continue;
        cache_learn(c, &ft, seq, len, ratio);
    }
    fclose(f);
    return c;
}

/* ---------- leave-one-out ---------- */

typedef struct {
    const char *path;
    FrontEnd *fe;
    Features f;
    int seq[SEARCH_MAX_SEQ];
    int len;
    long unopt, fixed, searched;
} Item;

int heuristic_leave_one_out(const char **paths, int n, int max_distance, FILE *out) {
    Item *items = calloc((size_t)(n > 0 ? n : 1), sizeof(Item));

    for (int i = 0; i < n; i++) {
        items[i].path = paths[i];
        char *src = pipeline_read_file(paths[i]);
        if (!src) { fprintf(out, "skip %s (cannot open)\n", paths[i]); continue; }
        FrontEnd *fe = frontend_run(src);
        free(src);
        if (fe->parse_errors || fe->semantic_errors) {
            fprintf(out, "skip %s (front-end errors)\n", paths[i]);
            frontend_free(fe);
            continue;
        }
        items[i].fe = fe;
        items[i].f = heuristic_features(fe->tac);

        SearchConfig cfg = search_default_config();
        SearchResult r = search_best_sequence(fe->tac, &cfg, NULL);
        items[i].len = r.sequence_len;
        memcpy(items[i].seq, r.sequence, sizeof(int) * (size_t)r.sequence_len);
        items[i].unopt = r.input_cost;
        items[i].fixed = r.baseline_cost;
        items[i].searched = r.best_cost;
        search_result_free(&r);
    }

    fprintf(out, "%-36s %-10s %8s %8s %8s %8s\n",
            "program", "lookup", "unopt", "fixed", "cached", "searched");
    int evaluated = 0, hits = 0;
    double ratio_sum = 0.0;
    int ratio_n = 0;

    for (int i = 0; i < n; i++) {
        if (!items[i].fe) continue;
        HeuristicCache *c = cache_create();
        for (int j = 0; j < n; j++) {
            if (j == i || !items[j].fe) continue;
            double ratio = items[j].fixed > 0 ? (double)items[j].searched / (double)items[j].fixed : 1.0;
            cache_learn(c, &items[j].f, items[j].seq, items[j].len, ratio);
        }

        int dist = 0;
        const CacheEntry *e = cache_lookup(c, &items[i].f, max_distance, &dist);
        long cached_cost = items[i].fixed;
        char how[32];
        if (e) {
            TACList *applied = apply_sequence(items[i].fe->tac, e->seq, e->len);
            long cost = interp_dynamic_cost(applied, NULL);
            taclist_free(applied);
            /* never worse than the baseline: fall back when the cached
             * sequence does not pay off on this program */
            if (cost >= 0 && cost <= items[i].fixed) cached_cost = cost;
            if (dist == 0) snprintf(how, sizeof how, "exact");
            else snprintf(how, sizeof how, "near(%d)", dist);
            hits++;
        } else {
            snprintf(how, sizeof how, "miss");
        }

        fprintf(out, "%-36s %-10s %8ld %8ld %8ld %8ld\n",
                items[i].path, how, items[i].unopt, items[i].fixed, cached_cost, items[i].searched);
        if (items[i].fixed > items[i].searched) {
            /* share of the search's gain over the fixed pipeline that the cache recovered */
            double gain = (double)(items[i].fixed - items[i].searched);
            double got = (double)(items[i].fixed - cached_cost);
            ratio_sum += got / gain;
            ratio_n++;
        }
        evaluated++;
        cache_free(c);
    }

    fprintf(out, "hit rate: %d/%d", hits, evaluated);
    if (ratio_n > 0)
        fprintf(out, "   search gain recovered by cache (programs where search beats fixed): %.0f%%",
                100.0 * ratio_sum / ratio_n);
    fprintf(out, "\n");

    for (int i = 0; i < n; i++) frontend_free(items[i].fe);
    free(items);
    return evaluated;
}
