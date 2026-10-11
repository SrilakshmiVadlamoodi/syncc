#include "optimizer.h"
#include "tacutil.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static char *int_to_str(long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    return tac_dup_str(buf);
}

static char *float_to_str(double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return tac_dup_str(buf);
}

static int is_comparison_op(TokenType op) {
    return op == TOK_EQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
}

/* Attempts to fold `a op b` into a single literal. Returns 1 and sets
 * *out (heap-allocated) on success; returns 0 (leaving *out untouched)
 * if the operator isn't foldable or would divide by zero -- callers
 * must fall back to keeping the original TAC_BINOP unchanged. */
static int fold_binop(TokenType op, const char *a, const char *b, char **out) {
    if (is_comparison_op(op)) {
        double av = atof(a), bv = atof(b);
        int result;
        switch (op) {
            case TOK_EQ: result = (av == bv); break;
            case TOK_LT: result = (av < bv); break;
            case TOK_GT: result = (av > bv); break;
            case TOK_LE: result = (av <= bv); break;
            case TOK_GE: result = (av >= bv); break;
            default: return 0;
        }
        *out = int_to_str(result);
        return 1;
    }

    int is_float = strchr(a, '.') != NULL || strchr(b, '.') != NULL;

    if (is_float) {
        double av = atof(a), bv = atof(b);
        double result;
        switch (op) {
            case TOK_PLUS:  result = av + bv; break;
            case TOK_MINUS: result = av - bv; break;
            case TOK_STAR:  result = av * bv; break;
            case TOK_SLASH:
                if (bv == 0.0) return 0;
                result = av / bv;
                break;
            default: return 0;
        }
        *out = float_to_str(result);
        return 1;
    }

    long av = atol(a), bv = atol(b);
    long result;
    switch (op) {
        case TOK_PLUS:  result = av + bv; break;
        case TOK_MINUS: result = av - bv; break;
        case TOK_STAR:  result = av * bv; break;
        case TOK_SLASH:
            if (bv == 0) return 0;
            result = av / bv;
            break;
        default: return 0;
    }
    *out = int_to_str(result);
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

/* Counts how many times `name` is written (as a TAC_ASSIGN/TAC_BINOP
 * result) within input[start, end). A count of 1 is what makes a name
 * safe to hoist: exactly one definition, so moving it can't disagree
 * with some other assignment left behind in the loop. */
static int name_assign_count(TACList *input, size_t start, size_t end, const char *name) {
    int count = 0;
    for (size_t i = start; i < end; i++) {
        TACInstr *in = &input->items[i];
        if ((in->op == TAC_ASSIGN || in->op == TAC_BINOP) && in->result && strcmp(in->result, name) == 0)
            count++;
    }
    return count;
}

TACList *optimize_loop_invariant_code_motion(TACList *input) {
    TACList *output = taclist_create();
    if (input->count == 0) return output;

    char *hoisted = calloc(input->count, sizeof(char));

    LoopInfo *loops = malloc(sizeof(LoopInfo) * (input->count + 1));
    size_t loop_count = 0;

    /* Pass 1: find every loop (a backward TAC_GOTO identifies the back
     * edge -- this codegen never emits a backward jump for anything
     * else), and for each straight-line body, mark which instructions
     * are safe to hoist into a preheader. */
    for (size_t i = 0; i < input->count; i++) {
        if (input->items[i].op != TAC_GOTO) continue;

        size_t header = input->count;
        for (size_t j = 0; j < i; j++) {
            if (input->items[j].op == TAC_LABEL && strcmp(input->items[j].result, input->items[i].result) == 0) {
                header = j;
                break;
            }
        }
        if (header == input->count) continue; /* forward goto (if/else), not a loop */

        size_t if_false = find_if_false_after(input, header + 1);
        if (if_false >= i) continue; /* malformed for this codegen; be safe and skip */

        size_t body_start = if_false + 1;
        size_t body_end = i;

        if (body_has_control_flow(input, body_start, body_end)) continue;

        for (size_t k = body_start; k < body_end; k++) {
            TACInstr *bi = &input->items[k];
            if (bi->op != TAC_ASSIGN && bi->op != TAC_BINOP) continue;
            if (name_assign_count(input, body_start, body_end, bi->result) != 1) continue;

            int invariant = 1;
            const char *operands[2] = { bi->arg1, bi->arg2 };
            for (int oi = 0; oi < 2 && invariant; oi++) {
                const char *val = operands[oi];
                if (!val || tac_is_numeric_literal(val)) continue;
                if (name_assign_count(input, body_start, body_end, val) == 0) continue; /* defined outside the loop */

                int found_hoisted = 0;
                for (size_t p = body_start; p < k; p++) {
                    if (hoisted[p] && input->items[p].result && strcmp(input->items[p].result, val) == 0) {
                        found_hoisted = 1;
                        break;
                    }
                }
                if (!found_hoisted) invariant = 0;
            }
            if (invariant) hoisted[k] = 1;
        }

        loops[loop_count++] = (LoopInfo){ .header = header, .body_start = body_start, .body_end = body_end };
    }

    /* Pass 2: emit, inserting each loop's hoisted instructions as a
     * preheader right before its header label and dropping them from
     * their original spot in the body. */
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

    free(loops);
    free(hoisted);
    return output;
}
