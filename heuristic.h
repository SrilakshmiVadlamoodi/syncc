#ifndef HEURISTIC_H
#define HEURISTIC_H

#include <stddef.h>
#include <stdio.h>
#include "codegen.h"
#include "search.h"

#define FEATURE_COUNT 7
#define CACHE_DEFAULT_MAX_DISTANCE 8

/* Quantized (0..5) counts: instrs, loops, mul, div, ifFalse,
 * foldable binops, copies. */
typedef struct { int v[FEATURE_COUNT]; } Features;

typedef struct {
    Features f;
    int seq[SEARCH_MAX_SEQ];
    int len;
    double ratio;   /* searched cost / fixed-pipeline cost, lower is better */
} CacheEntry;

typedef struct {
    CacheEntry *items;
    size_t count;
    size_t cap;
} HeuristicCache;

Features heuristic_features(const TACList *list);
int feature_distance(const Features *a, const Features *b);

HeuristicCache *cache_create(void);
HeuristicCache *cache_load(const char *path);   /* missing file -> empty cache */
int cache_save(const HeuristicCache *c, const char *path);
void cache_free(HeuristicCache *c);

/* Same features already present: keep whichever has the lower ratio. */
void cache_learn(HeuristicCache *c, const Features *f, const int *seq, int len, double ratio);

/* Nearest entry within max_distance (ties: lower ratio), else NULL. */
const CacheEntry *cache_lookup(const HeuristicCache *c, const Features *f,
                               int max_distance, int *distance_out);

/* Leave-one-out evaluation over `paths`: for each program, train a cache on
 * all the OTHER programs and compile it from the cache alone. Prints one
 * row per program and a summary. Returns how many programs were evaluated. */
int heuristic_leave_one_out(const char **paths, int n, int max_distance, FILE *out);

#endif
