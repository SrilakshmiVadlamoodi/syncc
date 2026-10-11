#ifndef COST_H
#define COST_H

#include <stdio.h>
#include "codegen.h"

typedef struct {
    int assign;
    int add_sub;
    int compare;
    int mul;
    int div;
    int if_false_goto;
    int jump;
    int label;
} CostModel;

extern const CostModel COST_MODEL_DEFAULT;

/* Weight of one executed instruction under COST_MODEL_DEFAULT. */
int cost_of_instr(const TACInstr *in);

/* Number of non-label instructions (static size). */
long cost_static_ops(const TACList *list);

/* One line listing every weight, for reports. */
void cost_model_print(FILE *out);

#endif
