#include "cost.h"

const CostModel COST_MODEL_DEFAULT = {
    .assign = 1, .add_sub = 1, .compare = 1, .mul = 3, .div = 10,
    .if_false_goto = 2, .jump = 1, .label = 0
};

int cost_of_instr(const TACInstr *in) {
    const CostModel *m = &COST_MODEL_DEFAULT;
    switch (in->op) {
        case TAC_ASSIGN:        return m->assign;
        case TAC_LABEL:         return m->label;
        case TAC_GOTO:          return m->jump;
        case TAC_IF_FALSE_GOTO: return m->if_false_goto;
        case TAC_BINOP:
            switch (in->binop) {
                case TOK_STAR:  return m->mul;
                case TOK_SLASH: return m->div;
                case TOK_PLUS:
                case TOK_MINUS: return m->add_sub;
                default:        return m->compare;
            }
    }
    return 0;
}

long cost_static_ops(const TACList *list) {
    long n = 0;
    for (size_t i = 0; i < list->count; i++)
        if (list->items[i].op != TAC_LABEL) n++;
    return n;
}

void cost_model_print(FILE *out) {
    const CostModel *m = &COST_MODEL_DEFAULT;
    fprintf(out, "cost weights: assign=%d add/sub=%d compare=%d mul=%d div=%d "
                 "ifFalse=%d goto=%d label=%d\n",
            m->assign, m->add_sub, m->compare, m->mul, m->div,
            m->if_false_goto, m->jump, m->label);
}
