#ifndef SUPPORT_H
#define SUPPORT_H

#include <stdio.h>
#include "codegen.h"
#include "semantic.h"
#include "test.h"

/* One instruction per line: "x = y", "x = y + z", "L1:", "goto L1",
 * "ifFalse x goto L1". Aborts on a malformed line. */
TACList *tac_from_text(const char *text);

/* "a b c" -> symbol table with those names as ints. */
SymbolTable *symtab_of(const char *names);

/* Capture helpers for functions that print to a FILE*. */
FILE *capture_open(void);
char *capture_close(FILE *f);   /* heap text; closes and deletes the file */

extern const char *SAMPLE_FILES[];  /* NULL-terminated */

#endif
