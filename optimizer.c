#include "optimizer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *int_to_str(long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    return dup_str(buf);
}

static char *float_to_str(double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return dup_str(buf);
}

/* True if `s` is a plain numeric literal ("5", "-3", "2.5") rather than
 * an identifier or temp name -- those always start with a letter/'_'/'t',
 * so this is unambiguous. */
static int is_numeric_literal(const char *s) {
    if (!s || !*s) return 0;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (!s[i]) return 0;
    int has_digit = 0;
    int has_dot = 0;
    for (; s[i]; i++) {
        if (isdigit((unsigned char)s[i])) {
            has_digit = 1;
        } else if (s[i] == '.' && !has_dot) {
            has_dot = 1;
        } else {
            return 0;
        }
    }
    return has_digit;
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

        if (in->op == TAC_BINOP && is_numeric_literal(in->arg1) && is_numeric_literal(in->arg2)) {
            char *folded = NULL;
            if (fold_binop(in->binop, in->arg1, in->arg2, &folded)) {
                TACInstr out = {
                    .op = TAC_ASSIGN,
                    .result = dup_str(in->result),
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
        TACInstr copy = {
            .op = in->op,
            .result = in->result ? dup_str(in->result) : NULL,
            .arg1 = in->arg1 ? dup_str(in->arg1) : NULL,
            .arg2 = in->arg2 ? dup_str(in->arg2) : NULL,
            .binop = in->binop
        };
        taclist_push(output, copy);
    }

    return output;
}
