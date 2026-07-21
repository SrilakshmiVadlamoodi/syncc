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

#endif
