#include "passes.h"

#include <stdlib.h>
#include <string.h>

#include "optimizer.h"
#include "strmap.h"
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

/* ================= common subexpression elimination ================= */

typedef struct { TokenType op; const char *a, *b, *holder; } Avail;
typedef struct { Avail *items; size_t count, cap; } AvailSet;

static const char *avail_find(const AvailSet *s, TokenType op, const char *a, const char *b) {
    int commutative = (op == TOK_PLUS || op == TOK_STAR);
    for (size_t i = 0; i < s->count; i++) {
        if (s->items[i].op != op) continue;
        if (!strcmp(s->items[i].a, a) && !strcmp(s->items[i].b, b)) return s->items[i].holder;
        if (commutative && !strcmp(s->items[i].a, b) && !strcmp(s->items[i].b, a))
            return s->items[i].holder;
    }
    return NULL;
}

static void avail_kill(AvailSet *s, const char *name) {
    size_t w = 0;
    for (size_t r = 0; r < s->count; r++) {
        const Avail *e = &s->items[r];
        if (!strcmp(e->holder, name) || !strcmp(e->a, name) || !strcmp(e->b, name)) continue;
        s->items[w++] = *e;
    }
    s->count = w;
}

static void avail_add(AvailSet *s, TokenType op, const char *a, const char *b, const char *holder) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->items = realloc(s->items, sizeof(Avail) * s->cap);
    }
    s->items[s->count++] = (Avail){ op, a, b, holder };
}

TACList *pass_cse(TACList *input) {
    TACList *out = taclist_create();
    AvailSet set = { NULL, 0, 0 };

    for (size_t i = 0; i < input->count; i++) {
        const TACInstr *in = &input->items[i];
        TACInstr c = tac_copy_instr(in);

        if (in->op == TAC_LABEL) {
            set.count = 0;
        } else if (in->op == TAC_BINOP) {
            const char *holder = avail_find(&set, in->binop, in->arg1, in->arg2);
            int replaced = 0;
            if (holder) {
                free(c.arg1);
                free(c.arg2);
                c.op = TAC_ASSIGN;
                c.arg1 = tac_dup_str(holder);
                c.arg2 = NULL;
                c.binop = 0;
                replaced = 1;
            }
            avail_kill(&set, in->result);
            if (!replaced && strcmp(in->result, in->arg1) != 0 && strcmp(in->result, in->arg2) != 0)
                avail_add(&set, in->binop, in->arg1, in->arg2, in->result);
        } else if (in->op == TAC_ASSIGN) {
            avail_kill(&set, in->result);
        } else if (is_control(in->op)) {
            set.count = 0;
        }
        taclist_push(out, c);
    }
    free(set.items);
    return out;
}

/* ================= dead code elimination ================= */

#define NO_PENDING ((size_t)-1)

static int reads_arg1(TACOp op) {
    return op == TAC_ASSIGN || op == TAC_BINOP || op == TAC_IF_FALSE_GOTO;
}

TACList *pass_dce(TACList *input) {
    size_t n = input->count;
    char *dead = calloc(n + 1, 1);

    /* (a) temps that are never read, to a fixed point */
    int changed = 1;
    while (changed) {
        changed = 0;
        StrMap *reads = strmap_create();
        for (size_t i = 0; i < n; i++) {
            if (dead[i]) continue;
            const TACInstr *in = &input->items[i];
            if (reads_arg1(in->op) && in->arg1 && !tac_is_numeric_literal(in->arg1))
                strmap_intern(reads, in->arg1);
            if (in->op == TAC_BINOP && in->arg2 && !tac_is_numeric_literal(in->arg2))
                strmap_intern(reads, in->arg2);
        }
        for (size_t i = 0; i < n; i++) {
            if (dead[i]) continue;
            const TACInstr *in = &input->items[i];
            if ((in->op == TAC_ASSIGN || in->op == TAC_BINOP) &&
                tac_is_temp_name(in->result) && strmap_find(reads, in->result) < 0) {
                dead[i] = 1;
                changed = 1;
            }
        }
        strmap_free(reads);
    }

    /* (b) a store overwritten later in the same block with no read between */
    size_t *pending = malloc(sizeof(size_t) * (n + 1));
    StrMap *names = strmap_create();
    for (size_t i = 0; i < n; i++) {
        if (dead[i]) continue;
        const TACInstr *in = &input->items[i];

        if (in->op == TAC_LABEL) {
            strmap_free(names);
            names = strmap_create();
            continue;
        }
        if (reads_arg1(in->op) && in->arg1 && !tac_is_numeric_literal(in->arg1)) {
            int id = strmap_find(names, in->arg1);
            if (id >= 0) pending[id] = NO_PENDING;
        }
        if (in->op == TAC_BINOP && in->arg2 && !tac_is_numeric_literal(in->arg2)) {
            int id = strmap_find(names, in->arg2);
            if (id >= 0) pending[id] = NO_PENDING;
        }
        if (in->op == TAC_ASSIGN || in->op == TAC_BINOP) {
            int before = (int)strmap_count(names);
            int id = strmap_intern(names, in->result);
            if (id == before) pending[id] = NO_PENDING;
            if (pending[id] != NO_PENDING) dead[pending[id]] = 1;
            pending[id] = i;
        }
        if (is_control(in->op)) {
            strmap_free(names);
            names = strmap_create();
        }
    }
    strmap_free(names);
    free(pending);

    TACList *out = taclist_create();
    for (size_t i = 0; i < n; i++)
        if (!dead[i]) taclist_push(out, tac_copy_instr(&input->items[i]));
    free(dead);
    return out;
}

/* ================= registry and helpers ================= */

const PassInfo PASS_REGISTRY[] = {
    { "fold",     optimize_constant_folding },
    { "licm",     optimize_loop_invariant_code_motion },
    { "copyprop", pass_copy_propagation },
    { "strength", pass_strength_reduction },
    { "cse",      pass_cse },
    { "dce",      pass_dce },
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
