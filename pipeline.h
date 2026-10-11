#ifndef PIPELINE_H
#define PIPELINE_H

#include "codegen.h"
#include "semantic.h"

typedef struct {
    TACList *tac;
    SymbolTable *symtab;
    int parse_errors;
    int semantic_errors;
} FrontEnd;

/* Quiet lex -> parse -> semantic -> codegen. Never returns NULL.
 * Caller frees with frontend_free. TAC is only meaningful when both
 * error counts are zero. */
FrontEnd *frontend_run(const char *source);
void frontend_free(FrontEnd *fe);

/* Heap copy of the file's contents, or NULL if it cannot be opened. */
char *pipeline_read_file(const char *path);

/* 1 if any user variable is spelled like a codegen temp (t<digits>). */
int frontend_has_temp_collision(const SymbolTable *symtab);

#endif
