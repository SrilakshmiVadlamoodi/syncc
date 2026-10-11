#ifndef FUZZ_H
#define FUZZ_H

#include <stdio.h>
#include "passes.h"

typedef struct {
    int programs;   /* generated */
    int rejected;   /* front-end errors, not checked */
    int checks;     /* (program, env, candidate) comparisons performed */
    int skipped;    /* environments where the original run did not finish OK */
    int failures;   /* candidates not equivalent to the original */
} FuzzStats;

/* Valid, closed, terminating mini-language source text. Same (seed, chunks)
 * always yields the same text. Caller frees. */
char *fuzz_generate_program(unsigned seed, int chunks);

/* Differential check of each pass, the fixed pipeline, all passes twice,
 * the searched sequence, and `extra` (may be NULL), against the
 * unoptimized TAC under `envs_per_program` random initial environments.
 * Report lines go to `out`. */
FuzzStats fuzz_run(int n, unsigned seed, int envs_per_program,
                   const PassInfo *extra, FILE *out);


/* Same differential check for one program given as source text. Used by
 * tests and for reproducing a reported failure. */
FuzzStats fuzz_check_source(const char *source, unsigned seed, int envs_per_program,
                            const PassInfo *extra, FILE *out);
#endif
