#ifndef EXPLAIN_H
#define EXPLAIN_H

#include <stdio.h>
#include "codegen.h"
#include "interp.h"

/* Applies PASS_REGISTRY[seq[0..len-1]] to `input` one at a time and prints,
 * for each step, the dynamic-cost change and an instruction-level diff,
 * then the total saved per pass. Costs use `initial` (may be NULL). */
void explain_sequence(TACList *input, const int *seq, int len,
                      const Env *initial, FILE *out);

#endif
