#ifndef TACUTIL_H
#define TACUTIL_H

#include "codegen.h"

char *tac_dup_str(const char *s);
TACInstr tac_copy_instr(const TACInstr *in);
TACList *tac_copy_list(const TACList *in);

int tac_is_numeric_literal(const char *s);
int tac_is_float_literal(const char *s);
int tac_is_temp_name(const char *s);

const char *tac_binop_symbol(TokenType op);
char *tac_instr_to_string(const TACInstr *in);   /* heap; caller frees */
char *tac_list_to_string(const TACList *list);   /* heap; one line per instr */

#endif
