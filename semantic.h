#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdio.h>
#include "parser.h"

typedef struct {
    char *name;
    DataType type;
    int declared_line;
} SymbolEntry;

/* Dynamic array of SymbolEntry, same growable-array pattern as TokenList/AstList. */
typedef struct {
    SymbolEntry *items;
    size_t count;
    size_t capacity;
} SymbolTable;

SymbolTable *symtab_create(void);
/* Returns the entry for `name`, or NULL if not present. */
SymbolEntry *symtab_lookup(SymbolTable *table, const char *name);
/* Inserts a new entry. Caller must symtab_lookup first to check for redeclaration. */
void symtab_insert(SymbolTable *table, const char *name, DataType type, int line);
void symtab_free(SymbolTable *table);

typedef enum {
    SEM_ERR_REDECLARATION,
    SEM_ERR_UNDECLARED,
    SEM_ERR_TYPE_MISMATCH
} SemanticErrorKind;

/* Structured like ParseError: no expected/found tokens here (semantic
 * errors aren't about token mismatches), just enough context for the
 * error engine to render a message. */
typedef struct {
    SemanticErrorKind kind;
    char *name;        /* variable name involved, heap copy */
    int line;
    DataType expected_type;  /* only meaningful for SEM_ERR_TYPE_MISMATCH */
    DataType found_type;     /* only meaningful for SEM_ERR_TYPE_MISMATCH */
} SemanticError;

typedef struct {
    SemanticError *items;
    size_t count;
    size_t capacity;
} SemanticErrorList;

SemanticErrorList *semantic_error_list_create(void);
void semantic_error_list_push(SemanticErrorList *list, SemanticError err);
void semantic_error_list_free(SemanticErrorList *list);

/* Walks the AST, builds the symbol table, and checks:
 *   - redeclaration (var declared twice in same scope)
 *   - use-before-declaration
 *   - type mismatch in assignment/expressions (int vs float)
 * Never aborts on error; keeps walking and collects every SemanticError
 * found, mirroring parser.c's panic-mode-but-keep-going approach.
 * Caller owns both the returned SymbolTable and errors_out. */
SymbolTable *analyze(AstNode *program, SemanticErrorList *errors_out);

/* Prints "name : type (declared line N)" per entry, for the driver's
 * "stage 3" output. */
void symtab_print(SymbolTable *table, FILE *out);

#endif
