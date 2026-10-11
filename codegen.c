#include "codegen.h"
#include "tacutil.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_CAPACITY 64

/* ================= TACList ================= */

TACList *taclist_create(void) {
    TACList *list = malloc(sizeof(TACList));
    list->items = malloc(sizeof(TACInstr) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

void taclist_push(TACList *list, TACInstr instr) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(TACInstr) * list->capacity);
    }
    list->items[list->count++] = instr;
}

void taclist_free(TACList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].result);
        free(list->items[i].arg1);
        free(list->items[i].arg2);
    }
    free(list->items);
    free(list);
}

/* ================= temp/label naming ================= */

static int temp_counter = 0;
static int label_counter = 0;

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

char *codegen_new_temp(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", ++temp_counter);
    return dup_str(buf);
}

char *codegen_new_label(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "L%d", ++label_counter);
    return dup_str(buf);
}

void codegen_reset_counters(void) {
    temp_counter = 0;
    label_counter = 0;
}

/* ================= AST -> TAC ================= */

static char *int_to_str(int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return dup_str(buf);
}

static char *float_to_str(double value) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", value);
    return dup_str(buf);
}

static void gen_stmt(AstNode *node, TACList *list);

/* Emits whatever instructions are needed to evaluate `node`, and returns
 * a heap-allocated operand string (a temp name, an identifier name, or a
 * literal) that the caller now owns -- either free it directly or place
 * it into a TACInstr field (which taclist_free will then own). */
static char *gen_expr(AstNode *node, TACList *list) {
    switch (node->type) {
        case NODE_NUM_INT:
            return int_to_str(node->as.num_int.value);
        case NODE_NUM_FLOAT:
            return float_to_str(node->as.num_float.value);
        case NODE_IDENT:
            return dup_str(node->as.ident.name);
        case NODE_BINOP: {
            char *left = gen_expr(node->as.binop.left, list);
            char *right = gen_expr(node->as.binop.right, list);
            char *temp = codegen_new_temp();

            TACInstr instr = {
                .op = TAC_BINOP,
                .result = dup_str(temp),
                .arg1 = left,
                .arg2 = right,
                .binop = node->as.binop.op
            };
            taclist_push(list, instr);
            return temp;
        }
        default:
            return dup_str(""); /* unreachable: not an expression node */
    }
}

static void gen_stmt(AstNode *node, TACList *list) {
    if (!node) return;

    switch (node->type) {
        case NODE_VAR_DECL:
            break; /* declarations don't emit TAC */

        case NODE_ASSIGN: {
            char *value_operand = gen_expr(node->as.assign.value, list);
            TACInstr instr = {
                .op = TAC_ASSIGN,
                .result = dup_str(node->as.assign.name),
                .arg1 = value_operand,
                .arg2 = NULL,
                .binop = 0
            };
            taclist_push(list, instr);
            break;
        }

        case NODE_IF: {
            char *cond = gen_expr(node->as.if_stmt.cond, list);
            char *else_label = codegen_new_label();

            taclist_push(list, (TACInstr){
                .op = TAC_IF_FALSE_GOTO, .result = dup_str(else_label),
                .arg1 = cond, .arg2 = NULL, .binop = 0
            });

            gen_stmt(node->as.if_stmt.then_branch, list);

            if (node->as.if_stmt.else_branch) {
                char *end_label = codegen_new_label();
                taclist_push(list, (TACInstr){
                    .op = TAC_GOTO, .result = dup_str(end_label),
                    .arg1 = NULL, .arg2 = NULL, .binop = 0
                });
                taclist_push(list, (TACInstr){
                    .op = TAC_LABEL, .result = else_label,
                    .arg1 = NULL, .arg2 = NULL, .binop = 0
                });
                gen_stmt(node->as.if_stmt.else_branch, list);
                taclist_push(list, (TACInstr){
                    .op = TAC_LABEL, .result = end_label,
                    .arg1 = NULL, .arg2 = NULL, .binop = 0
                });
            } else {
                taclist_push(list, (TACInstr){
                    .op = TAC_LABEL, .result = else_label,
                    .arg1 = NULL, .arg2 = NULL, .binop = 0
                });
            }
            break;
        }

        case NODE_WHILE: {
            char *start_label = codegen_new_label();
            char *end_label = codegen_new_label();

            taclist_push(list, (TACInstr){
                .op = TAC_LABEL, .result = dup_str(start_label),
                .arg1 = NULL, .arg2 = NULL, .binop = 0
            });

            char *cond = gen_expr(node->as.while_stmt.cond, list);
            taclist_push(list, (TACInstr){
                .op = TAC_IF_FALSE_GOTO, .result = dup_str(end_label),
                .arg1 = cond, .arg2 = NULL, .binop = 0
            });

            gen_stmt(node->as.while_stmt.body, list);

            taclist_push(list, (TACInstr){
                .op = TAC_GOTO, .result = start_label,
                .arg1 = NULL, .arg2 = NULL, .binop = 0
            });
            taclist_push(list, (TACInstr){
                .op = TAC_LABEL, .result = end_label,
                .arg1 = NULL, .arg2 = NULL, .binop = 0
            });
            break;
        }

        case NODE_BLOCK:
            for (size_t i = 0; i < node->as.block.statements->count; i++)
                gen_stmt(node->as.block.statements->items[i], list);
            break;

        default:
            break; /* expression nodes never appear directly as statements */
    }
}

TACList *generate_tac(AstNode *program) {
    TACList *list = taclist_create();
    for (size_t i = 0; i < program->as.program.statements->count; i++) {
        gen_stmt(program->as.program.statements->items[i], list);
    }
    return list;
}

/* ================= TAC print ================= */

void tac_print(TACList *list, FILE *out) {
    for (size_t i = 0; i < list->count; i++) {
        TACInstr *instr = &list->items[i];
        switch (instr->op) {
            case TAC_ASSIGN:
                fprintf(out, "%s = %s\n", instr->result, instr->arg1);
                break;
            case TAC_BINOP:
                fprintf(out, "%s = %s %s %s\n", instr->result, instr->arg1,
                        tac_binop_symbol(instr->binop), instr->arg2);
                break;
            case TAC_LABEL:
                fprintf(out, "%s:\n", instr->result);
                break;
            case TAC_GOTO:
                fprintf(out, "goto %s\n", instr->result);
                break;
            case TAC_IF_FALSE_GOTO:
                fprintf(out, "ifFalse %s goto %s\n", instr->arg1, instr->result);
                break;
        }
    }
}
