#include "passes.h"

#include <stdlib.h>
#include <string.h>

#include "optimizer.h"
#include "tacutil.h"

static int is_control(TACOp op) {
    return op == TAC_LABEL || op == TAC_GOTO || op == TAC_IF_FALSE_GOTO;
}

/* ================= copy propagation ================= */

typedef struct { const char *key; const char *val; } CopyEntry;
typedef struct { CopyEntry *items; size_t count, cap; } CopyMap;

static const char *copy_lookup(const CopyMap *m, const char *operand) {
    if (!operand || tac_is_numeric_literal(operand)) return operand;
    for (size_t i = 0; i < m->count; i++)
        if (strcmp(m->items[i].key, operand) == 0) return m->items[i].val;
    return operand;
}

static void copy_kill(CopyMap *m, const char *name) {
    size_t w = 0;
    for (size_t r = 0; r < m->count; r++) {
        if (strcmp(m->items[r].key, name) == 0 || strcmp(m->items[r].val, name) == 0) continue;
        m->items[w++] = m->items[r];
    }
    m->count = w;
}

static void copy_add(CopyMap *m, const char *key, const char *val) {
    if (m->count == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->items = realloc(m->items, sizeof(CopyEntry) * m->cap);
    }
    m->items[m->count++] = (CopyEntry){ key, val };
}

TACList *pass_copy_propagation(TACList *input) {
    TACList *out = taclist_create();
    CopyMap map = { NULL, 0, 0 };

    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_LABEL) {
            map.count = 0;
        } else {
            if (in->op == TAC_ASSIGN || in->op == TAC_BINOP || in->op == TAC_IF_FALSE_GOTO) {
                const char *r1 = copy_lookup(&map, in->arg1);
                if (r1 != in->arg1) { free(c.arg1); c.arg1 = tac_dup_str(r1); }
            }
            if (in->op == TAC_BINOP) {
                const char *r2 = copy_lookup(&map, in->arg2);
                if (r2 != in->arg2) { free(c.arg2); c.arg2 = tac_dup_str(r2); }
            }
            if (in->op == TAC_ASSIGN || in->op == TAC_BINOP) {
                /* Resolve the copy source against the map BEFORE the write
                 * kills entries; `x = y` then records x -> source. */
                const char *src = in->op == TAC_ASSIGN ? copy_lookup(&map, in->arg1) : NULL;
                copy_kill(&map, in->result);
                if (src && strcmp(src, in->result) != 0) copy_add(&map, in->result, src);
            }
            if (is_control(in->op)) map.count = 0;
        }
        taclist_push(out, c);
    }
    free(map.items);
    return out;
}

/* ================= strength reduction ================= */

static int is_lit(const char *s, const char *want) {
    return s != NULL && strcmp(s, want) == 0;
}

TACList *pass_strength_reduction(TACList *input) {
    TACList *out = taclist_create();
    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_BINOP) {
            const char *a = in->arg1, *b = in->arg2;
            TokenType op = in->binop;

            if (op == TOK_STAR && is_lit(b, "2")) {            /* a*2 -> a+a */
                free(c.arg2);
                c.arg2 = tac_dup_str(a);
                c.binop = TOK_PLUS;
            } else if (op == TOK_STAR && is_lit(a, "2")) {     /* 2*b -> b+b */
                free(c.arg1);
                c.arg1 = tac_dup_str(b);
                c.binop = TOK_PLUS;
            } else if ((op == TOK_STAR && is_lit(b, "1")) ||
                       (op == TOK_PLUS && is_lit(b, "0")) ||
                       (op == TOK_MINUS && is_lit(b, "0")) ||
                       (op == TOK_SLASH && is_lit(b, "1"))) {  /* result = a */
                free(c.arg2);
                c.arg2 = NULL;
                c.op = TAC_ASSIGN;
                c.binop = 0;
            } else if ((op == TOK_STAR && is_lit(a, "1")) ||
                       (op == TOK_PLUS && is_lit(a, "0"))) {   /* result = b */
                free(c.arg1);
                c.arg1 = c.arg2;
                c.arg2 = NULL;
                c.op = TAC_ASSIGN;
                c.binop = 0;
            }
        }
        taclist_push(out, c);
    }
    return out;
}

/* ================= registry and helpers ================= */

const PassInfo PASS_REGISTRY[] = {
    { "fold",     optimize_constant_folding },
    { "licm",     optimize_loop_invariant_code_motion },
    { "copyprop", pass_copy_propagation },
    { "strength", pass_strength_reduction },
    { NULL, NULL }
};

size_t pass_count(void) {
    size_t n = 0;
    while (PASS_REGISTRY[n].name) n++;
    return n;
}

int pass_index(const char *name) {
    for (size_t i = 0; PASS_REGISTRY[i].name; i++)
        if (strcmp(PASS_REGISTRY[i].name, name) == 0) return (int)i;
    return -1;
}

TACList *apply_sequence(TACList *input, const int *seq, int len) {
    TACList *cur = tac_copy_list(input);
    for (int i = 0; i < len; i++) {
        TACList *next = PASS_REGISTRY[seq[i]].fn(cur);
        taclist_free(cur);
        cur = next;
    }
    return cur;
}

TACList *apply_fixed_pipeline(TACList *input) {
    TACList *folded = optimize_constant_folding(input);
    TACList *hoisted = optimize_loop_invariant_code_motion(folded);
    taclist_free(folded);
    return hoisted;
}
