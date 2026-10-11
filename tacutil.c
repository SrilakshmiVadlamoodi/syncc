#include "tacutil.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *tac_dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len + 1);
    return out;
}

TACInstr tac_copy_instr(const TACInstr *in) {
    return (TACInstr){
        .op = in->op,
        .result = in->result ? tac_dup_str(in->result) : NULL,
        .arg1 = in->arg1 ? tac_dup_str(in->arg1) : NULL,
        .arg2 = in->arg2 ? tac_dup_str(in->arg2) : NULL,
        .binop = in->binop
    };
}

TACList *tac_copy_list(const TACList *in) {
    TACList *out = taclist_create();
    for (size_t i = 0; i < in->count; i++)
        taclist_push(out, tac_copy_instr(&in->items[i]));
    return out;
}

int tac_is_numeric_literal(const char *s) {
    if (!s || !*s) return 0;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (!s[i]) return 0;
    int has_digit = 0, has_dot = 0;
    for (; s[i]; i++) {
        if (isdigit((unsigned char)s[i])) has_digit = 1;
        else if (s[i] == '.' && !has_dot) has_dot = 1;
        else return 0;
    }
    return has_digit;
}

int tac_is_float_literal(const char *s) {
    return tac_is_numeric_literal(s) && strchr(s, '.') != NULL;
}

int tac_is_temp_name(const char *s) {
    if (!s || s[0] != 't' || !s[1]) return 0;
    for (size_t i = 1; s[i]; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

const char *tac_binop_symbol(TokenType op) {
    switch (op) {
        case TOK_PLUS:  return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR:  return "*";
        case TOK_SLASH: return "/";
        case TOK_EQ:    return "==";
        case TOK_LT:    return "<";
        case TOK_GT:    return ">";
        case TOK_LE:    return "<=";
        case TOK_GE:    return ">=";
        default:        return "?";
    }
}

char *tac_instr_to_string(const TACInstr *in) {
    const char *r = in->result ? in->result : "";
    const char *a = in->arg1 ? in->arg1 : "";
    const char *b = in->arg2 ? in->arg2 : "";
    size_t cap = strlen(r) + strlen(a) + strlen(b) + 32;
    char *out = malloc(cap);
    switch (in->op) {
        case TAC_ASSIGN:
            snprintf(out, cap, "%s = %s", r, a);
            break;
        case TAC_BINOP:
            snprintf(out, cap, "%s = %s %s %s", r, a, tac_binop_symbol(in->binop), b);
            break;
        case TAC_LABEL:
            snprintf(out, cap, "%s:", r);
            break;
        case TAC_GOTO:
            snprintf(out, cap, "goto %s", r);
            break;
        case TAC_IF_FALSE_GOTO:
            snprintf(out, cap, "ifFalse %s goto %s", a, r);
            break;
    }
    return out;
}

char *tac_list_to_string(const TACList *list) {
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    out[0] = '\0';
    for (size_t i = 0; i < list->count; i++) {
        char *line = tac_instr_to_string(&list->items[i]);
        size_t n = strlen(line);
        while (len + n + 2 > cap) { cap *= 2; out = realloc(out, cap); }
        memcpy(out + len, line, n);
        len += n;
        out[len++] = '\n';
        out[len] = '\0';
        free(line);
    }
    return out;
}

char *tac_format_double(double v) {
    char buf[800];
    int ok = 0;
    if (!isfinite(v)) return NULL;

    /* shortest %g form that parses back exactly */
    for (int prec = 1; prec <= 17 && !ok; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (strtod(buf, NULL) == v) ok = 1;
    }
    /* %g switched to exponent notation, which a TAC literal cannot hold:
     * use plain decimal digits with just enough fractional places */
    if (!ok || strpbrk(buf, "eE") != NULL) {
        ok = 0;
        for (int dec = 0; dec <= 340 && !ok; dec++) {
            snprintf(buf, sizeof buf, "%.*f", dec, v);
            if (strtod(buf, NULL) == v) ok = 1;
        }
    }
    if (!ok) return NULL;
    if (strchr(buf, '.') == NULL) strcat(buf, ".0");
    return tac_dup_str(buf);
}
