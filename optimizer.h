#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "codegen.h"

/* Constant folding only (dead code elimination dropped from scope).
 * Scans TAC_BINOP instructions whose arg1/arg2 are both numeric-literal
 * strings and rewrites them to a TAC_ASSIGN with the computed literal,
 * e.g. "t1 = 2 + 3"  ->  "t1 = 5".
 *
 * Returns a NEW TACList (the input is left untouched) so the driver can
 * print unoptimized vs. optimized TAC side by side. Caller owns the
 * returned list. */
TACList *optimize_constant_folding(TACList *input);

/* Loop-invariant code motion (LICM) over TAC.
 *
 * Detects `while` loops by their back-edge (a TAC_GOTO jumping to a
 * TAC_LABEL that appears earlier in the list -- the only place this
 * codegen emits a backward jump). For each loop whose body is a single
 * straight-line block (no nested TAC_LABEL/TAC_GOTO/TAC_IF_FALSE_GOTO --
 * i.e. no nested if/while), hoists any TAC_ASSIGN/TAC_BINOP instruction
 * whose operands are all loop-invariant, whose result is a compiler temp
 * (t<N>) assigned exactly once in the body, and which is not a division
 * by a non-literal or zero divisor, into a preheader placed just before
 * the loop's label. User-variable assignments are never hoisted: the
 * loop may run zero times, or read the variable before assigning it.
 *
 * Loops with nested control flow in the body are left completely
 * unchanged -- deciding whether a conditionally-executed assignment is
 * safe to hoist unconditionally needs real basic-block/dominator
 * analysis, which is out of scope here; skipping is the safe fallback.
 *
 * Returns a NEW TACList; input is left untouched. Caller owns the
 * returned list. */
TACList *optimize_loop_invariant_code_motion(TACList *input);

#endif
