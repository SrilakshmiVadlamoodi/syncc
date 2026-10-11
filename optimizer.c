#include "optimizer.h"
#include "strmap.h"
#include "tacutil.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>

static char *int_to_str(long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    return tac_dup_str(buf);
}

static int is_comparison_op(TokenType op) {
    return op == TOK_EQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
}

/* Formats `v` as a float literal that parses back to exactly `v`, always
 * containing a '.' so it stays a float literal. Fails (returns 0) for
 * non-finite values and for values that would print in exponent form,
 * because tac_is_numeric_literal cannot represent those. */
static int format_float_literal(double v, char **out) {
    char buf[64];
    if (!isfinite(v)) return 0;
    for (int prec = 6; prec <= 17; prec++) {
        snprintf(buf, sizeof(buf), "%.*g", prec, v);
        if (strtod(buf, NULL) == v) break;
    }
    if (strpbrk(buf, "eE") != NULL) return 0;
    if (strchr(buf, '.') == NULL) strcat(buf, ".0");
    *out = tac_dup_str(buf);
    return 1;
}

/* Attempts to fold `a op b` into a single literal. Returns 1 and sets
 * *out (heap-allocated) on success; returns 0 (leaving *out untouched)
 * when the operator isn't foldable, would divide by zero, would overflow
 * integer arithmetic, or the float result cannot be written back as a
 * plain literal -- callers keep the original TAC_BINOP in that case. */
static int fold_binop(TokenType op, const char *a, const char *b, char **out) {
    int a_float = tac_is_float_literal(a);
    int b_float = tac_is_float_literal(b);

    if (is_comparison_op(op)) {
        int r;
        if (!a_float && !b_float) {
            long x = strtol(a, NULL, 10), y = strtol(b, NULL, 10);
            switch (op) {
                case TOK_EQ: r = (x == y); break;
                case TOK_LT: r = (x < y); break;
                case TOK_GT: r = (x > y); break;
                case TOK_LE: r = (x <= y); break;
                case TOK_GE: r = (x >= y); break;
                default: return 0;
            }
        } else {
            double x = atof(a), y = atof(b);
            switch (op) {
                case TOK_EQ: r = (x == y); break;
                case TOK_LT: r = (x < y); break;
                case TOK_GT: r = (x > y); break;
                case TOK_LE: r = (x <= y); break;
                case TOK_GE: r = (x >= y); break;
                default: return 0;
            }
        }
        *out = int_to_str(r);
        return 1;
    }

    if (a_float || b_float) {
        double x = atof(a), y = atof(b), r;
        switch (op) {
            case TOK_PLUS:  r = x + y; break;
            case TOK_MINUS: r = x - y; break;
            case TOK_STAR:  r = x * y; break;
            case TOK_SLASH:
                if (y == 0.0) return 0;
                r = x / y;
                break;
            default: return 0;
        }
        return format_float_literal(r, out);
    }

    long x = strtol(a, NULL, 10), y = strtol(b, NULL, 10), r;
    switch (op) {
        case TOK_PLUS:  if (__builtin_add_overflow(x, y, &r)) return 0; break;
        case TOK_MINUS: if (__builtin_sub_overflow(x, y, &r)) return 0; break;
        case TOK_STAR:  if (__builtin_mul_overflow(x, y, &r)) return 0; break;
        case TOK_SLASH:
            if (y == 0 || (x == LONG_MIN && y == -1)) return 0;
            r = x / y;
            break;
        default: return 0;
    }
    *out = int_to_str(r);
    return 1;
}

TACList *optimize_constant_folding(TACList *input) {
    TACList *output = taclist_create();

    for (size_t i = 0; i < input->count; i++) {
        TACInstr *in = &input->items[i];

        if (in->op == TAC_BINOP && tac_is_numeric_literal(in->arg1) && tac_is_numeric_literal(in->arg2)) {
            char *folded = NULL;
            if (fold_binop(in->binop, in->arg1, in->arg2, &folded)) {
                TACInstr out = {
                    .op = TAC_ASSIGN,
                    .result = tac_dup_str(in->result),
                    .arg1 = folded,
                    .arg2 = NULL,
                    .binop = 0
                };
                taclist_push(output, out);
                continue;
            }
        }

        /* unchanged: deep-copy so `output` fully owns its own strings,
         * independent of `input` (which the caller may free separately) */
        taclist_push(output, tac_copy_instr(in));
    }

    return output;
}

/* ================= Loop-invariant code motion ================= */

typedef struct {
    size_t header;      /* index of the TAC_LABEL starting the loop */
    size_t body_start;  /* first instruction after the ifFalse guard */
    size_t body_end;    /* exclusive: index of the back-edge TAC_GOTO */
} LoopInfo;

static size_t find_if_false_after(TACList *input, size_t from) {
    for (size_t i = from; i < input->count; i++) {
        if (input->items[i].op == TAC_IF_FALSE_GOTO) return i;
    }
    return input->count;
}

static int body_has_control_flow(TACList *input, size_t start, size_t end) {
    for (size_t i = start; i < end; i++) {
        TACOp op = input->items[i].op;
        if (op == TAC_LABEL || op == TAC_GOTO || op == TAC_IF_FALSE_GOTO) return 1;
    }
    return 0;
}

/* Loops are discovered in order of their back-edges and processed in that
 * order; with straight-line bodies only (no nesting), headers ascend, which
 * pass 2 relies on. */
TACList *optimize_loop_invariant_code_motion(TACList *input) {
    TACList *output = taclist_create();
    if (input->count == 0) return output;

    char *hoisted = calloc(input->count, sizeof(char));
    LoopInfo *loops = malloc(sizeof(LoopInfo) * (input->count + 1));
    size_t loop_count = 0;

    /* label name -> index of its TAC_LABEL */
    StrMap *labels = strmap_create();
    size_t *label_pos = malloc(sizeof(size_t) * (input->count + 1));
    for (size_t i = 0; i < input->count; i++) {
        if (input->items[i].op != TAC_LABEL) continue;
        int before = (int)strmap_count(labels);
        int id = strmap_intern(labels, input->items[i].result);
        if (id == before) label_pos[id] = i;
    }

    /* Pass 1: find every loop (a backward TAC_GOTO is the back edge) and,
     * for each straight-line body, mark which instructions are safe to
     * hoist into a preheader. */
    for (size_t i = 0; i < input->count; i++) {
        if (input->items[i].op != TAC_GOTO) continue;

        int label_id = strmap_find(labels, input->items[i].result);
        if (label_id < 0 || label_pos[label_id] >= i) continue; /* forward goto (if/else) */
        size_t header = label_pos[label_id];

        size_t if_false = find_if_false_after(input, header + 1);
        if (if_false >= i) continue;

        size_t body_start = if_false + 1;
        size_t body_end = i;
        if (body_has_control_flow(input, body_start, body_end)) continue;

        /* One pass over the body: how many times is each name written? */
        StrMap *defs = strmap_create();
        int *def_count = calloc(body_end - body_start + 1, sizeof(int));
        for (size_t k = body_start; k < body_end; k++) {
            TACInstr *bi = &input->items[k];
            if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;
            int before = (int)strmap_count(defs);
            int id = strmap_intern(defs, bi->result);
            if (id == before) def_count[id] = 0;
            def_count[id]++;
        }
        StrMap *hoisted_defs = strmap_create();

        for (size_t k = body_start; k < body_end; k++) {
            TACInstr *bi = &input->items[k];
            if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;
            /* Only compiler temps are hoisted: a user variable may be read
             * before its assignment in the body, or be observed after a loop
             * that never runs. */
            if (!tac_is_temp_name(bi->result)) continue;
            /* A hoisted division could trap on a loop that never executes. */
            if (bi->op == TAC_BINOP && bi->binop == TOK_SLASH &&
                !(tac_is_numeric_literal(bi->arg2) && atof(bi->arg2) != 0.0)) continue;
            int rid = strmap_find(defs, bi->result);
            if (rid < 0 || def_count[rid] != 1) continue;

            int invariant = 1;
            const char *operands[2] = { bi->arg1, bi->arg2 };
            for (int oi = 0; oi < 2 && invariant; oi++) {
                const char *val = operands[oi];
                if (!val || tac_is_numeric_literal(val)) continue;
                if (strmap_find(defs, val) < 0) continue;              /* defined outside the loop */
                if (strmap_find(hoisted_defs, val) < 0) invariant = 0; /* defined here, not hoisted */
            }
            if (invariant) {
                hoisted[k] = 1;
                strmap_intern(hoisted_defs, bi->result);
            }
        }

        strmap_free(hoisted_defs);
        free(def_count);
        strmap_free(defs);

        loops[loop_count++] = (LoopInfo){ .header = header, .body_start = body_start, .body_end = body_end };
    }

    /* Pass 2: emit, inserting each loop's hoisted instructions as a
     * preheader right before its header label. */
    size_t next_loop = 0;
    for (size_t i = 0; i < input->count; i++) {
        if (next_loop < loop_count && i == loops[next_loop].header) {
            for (size_t k = loops[next_loop].body_start; k < loops[next_loop].body_end; k++) {
                if (hoisted[k]) taclist_push(output, tac_copy_instr(&input->items[k]));
            }
            next_loop++;
        }
        if (hoisted[i]) continue;
        taclist_push(output, tac_copy_instr(&input->items[i]));
    }

    strmap_free(labels);
    free(label_pos);
    free(loops);
    free(hoisted);
    return output;
}
