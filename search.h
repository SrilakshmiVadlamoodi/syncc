#ifndef SEARCH_H
#define SEARCH_H

#include <stdio.h>
#include "codegen.h"
#include "interp.h"

#define SEARCH_MAX_SEQ 16

typedef struct {
    int max_depth;   /* sequence length bound */
    int budget;      /* max candidate evaluations */
} SearchConfig;

typedef struct {
    TACList *best;                    /* owned */
    int sequence[SEARCH_MAX_SEQ];     /* indices into PASS_REGISTRY */
    int sequence_len;
    long best_cost;                   /* dynamic cost of `best` */
    long baseline_cost;               /* dynamic cost of the fixed fold -> licm pipeline */
    long input_cost;                  /* dynamic cost of the unoptimized input */
    int candidates;                   /* distinct states evaluated */
    int invalid_candidates;           /* states that failed to run to completion (a pass bug) */
    double seconds;
} SearchResult;

SearchConfig search_default_config(void);   /* depth 4, budget 2000 */

/* Breadth-first over pass sequences from `input`, memoized by TAC hash,
 * scored by the interpreter under `initial` (may be NULL). If the input
 * itself does not run to completion the result is a copy of the input with
 * an empty sequence. */
SearchResult search_best_sequence(TACList *input, const SearchConfig *cfg, const Env *initial);
void search_result_free(SearchResult *r);

/* "fold, copyprop, dce" or "(none)". */
void search_print_sequence(const SearchResult *r, FILE *out);

#endif
