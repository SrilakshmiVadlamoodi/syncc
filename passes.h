#ifndef PASSES_H
#define PASSES_H

#include <stddef.h>
#include "codegen.h"

/* Every pass returns a NEW list and leaves `input` untouched. */
typedef TACList *(*PassFn)(TACList *input);

typedef struct {
    const char *name;
    PassFn fn;
} PassInfo;

/* NULL-name terminated; order is stable (indices are used in sequences). */
extern const PassInfo PASS_REGISTRY[];
size_t pass_count(void);
int pass_index(const char *name);   /* -1 if unknown */

/* Block-scoped passes (a block ends at a label / goto / ifFalse). */
TACList *pass_copy_propagation(TACList *input);
TACList *pass_strength_reduction(TACList *input);
TACList *pass_cse(TACList *input);
TACList *pass_dce(TACList *input);

/* Applies PASS_REGISTRY[seq[0]], then seq[1], ... Returns a new list; an
 * empty sequence returns a copy of `input`. */
TACList *apply_sequence(TACList *input, const int *seq, int len);

/* The original fixed pipeline: constant folding, then LICM. */
TACList *apply_fixed_pipeline(TACList *input);

#endif
