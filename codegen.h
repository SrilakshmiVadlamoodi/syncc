#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "parser.h"

typedef enum {
    TAC_ASSIGN,        /* result = arg1                (arg2, op unused) */
    TAC_BINOP,         /* result = arg1 op arg2 */
    TAC_LABEL,         /* result: (a label definition; arg1/arg2/op unused) */
    TAC_GOTO,          /* goto result                  (result holds label name) */
    TAC_IF_FALSE_GOTO  /* ifFalse arg1 goto result      (result holds label name) */
} TACOp;

/* All operands are stored as text (var name, temp name like "t1", or a
 * literal like "5"/"3.14") so printing TAC is just string concatenation
 * and constant folding in optimizer.c can pattern-match on arg1/arg2
 * being numeric strings. Fields not used by a given `op` are NULL. */
typedef struct {
    TACOp op;
    char *result;
    char *arg1;
    char *arg2;
    TokenType binop;   /* only valid when op == TAC_BINOP */
} TACInstr;

typedef struct {
    TACInstr *items;
    size_t count;
    size_t capacity;
} TACList;

TACList *taclist_create(void);
void taclist_push(TACList *list, TACInstr instr);
void taclist_free(TACList *list); /* frees the heap-copied operand strings too */

/* Fresh "t<N>" temp / "L<N>" label names for this codegen run. Each call
 * returns a new heap-allocated string owned by the caller (normally
 * handed straight into a TACInstr field). */
char *codegen_new_temp(void);
char *codegen_new_label(void);
void codegen_reset_counters(void); /* call once per compile so temp/label numbering restarts */

/* Walks the (semantically valid) AST and emits three-address code.
 * Caller owns the returned TACList. */
TACList *generate_tac(AstNode *program);

/* Prints one instruction per line in a simple textual format, e.g.:
 *   t1 = x - 1
 *   x = t1
 *   L1:
 *   ifFalse t2 goto L2
 *   goto L1
 */
void tac_print(TACList *list, FILE *out);

#endif
