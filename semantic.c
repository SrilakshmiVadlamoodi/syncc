#include "semantic.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

/* ================= SymbolTable ================= */

SymbolTable *symtab_create(void) {
    SymbolTable *table = malloc(sizeof(SymbolTable));
    table->items = malloc(sizeof(SymbolEntry) * INITIAL_CAPACITY);
    table->count = 0;
    table->capacity = INITIAL_CAPACITY;
    return table;
}

SymbolEntry *symtab_lookup(SymbolTable *table, const char *name) {
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->items[i].name, name) == 0) return &table->items[i];
    }
    return NULL;
}

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

void symtab_insert(SymbolTable *table, const char *name, DataType type, int line) {
    if (table->count == table->capacity) {
        table->capacity *= 2;
        table->items = realloc(table->items, sizeof(SymbolEntry) * table->capacity);
    }
    table->items[table->count].name = dup_str(name);
    table->items[table->count].type = type;
    table->items[table->count].declared_line = line;
    table->count++;
}

void symtab_free(SymbolTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->count; i++) free(table->items[i].name);
    free(table->items);
    free(table);
}

static const char *type_name(DataType type) {
    switch (type) {
        case TYPE_INT:   return "int";
        case TYPE_FLOAT: return "float";
        default:         return "unknown";
    }
}

void symtab_print(SymbolTable *table, FILE *out) {
    for (size_t i = 0; i < table->count; i++) {
        SymbolEntry *e = &table->items[i];
        fprintf(out, "  %-12s : %-6s (declared line %d)\n", e->name, type_name(e->type), e->declared_line);
    }
}

/* ================= SemanticErrorList ================= */

SemanticErrorList *semantic_error_list_create(void) {
    SemanticErrorList *list = malloc(sizeof(SemanticErrorList));
    list->items = malloc(sizeof(SemanticError) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

void semantic_error_list_push(SemanticErrorList *list, SemanticError err) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(SemanticError) * list->capacity);
    }
    list->items[list->count++] = err;
}

void semantic_error_list_free(SemanticErrorList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i].name);
    free(list->items);
    free(list);
}

/* ================= analysis ================= */

static void analyze_stmt(AstNode *node, SymbolTable *table, SemanticErrorList *errors);

/* Computes the type of an expression, flagging undeclared identifiers
 * along the way. Returns TYPE_UNKNOWN if the expression (or a
 * sub-expression) couldn't be resolved, which callers treat as "already
 * reported, don't cascade another error". */
static DataType compute_expr_type(AstNode *node, SymbolTable *table, SemanticErrorList *errors) {
    switch (node->type) {
        case NODE_NUM_INT:
            return TYPE_INT;
        case NODE_NUM_FLOAT:
            return TYPE_FLOAT;
        case NODE_IDENT: {
            SymbolEntry *entry = symtab_lookup(table, node->as.ident.name);
            if (!entry) {
                SemanticError err = {
                    .kind = SEM_ERR_UNDECLARED,
                    .name = dup_str(node->as.ident.name),
                    .line = node->line,
                    .expected_type = TYPE_UNKNOWN,
                    .found_type = TYPE_UNKNOWN
                };
                semantic_error_list_push(errors, err);
                return TYPE_UNKNOWN;
            }
            return entry->type;
        }
        case NODE_BINOP: {
            DataType lt = compute_expr_type(node->as.binop.left, table, errors);
            DataType rt = compute_expr_type(node->as.binop.right, table, errors);
            if (lt == TYPE_UNKNOWN || rt == TYPE_UNKNOWN) return TYPE_UNKNOWN;
            /* int op int -> int; anything involving float -> float
             * (ordinary arithmetic promotion, not an error). */
            return (lt == TYPE_FLOAT || rt == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
        }
        default:
            return TYPE_UNKNOWN; /* unreachable: not an expression node */
    }
}

static void analyze_stmt(AstNode *node, SymbolTable *table, SemanticErrorList *errors) {
    if (!node) return;

    switch (node->type) {
        case NODE_VAR_DECL: {
            if (symtab_lookup(table, node->as.var_decl.name)) {
                SemanticError err = {
                    .kind = SEM_ERR_REDECLARATION,
                    .name = dup_str(node->as.var_decl.name),
                    .line = node->line,
                    .expected_type = TYPE_UNKNOWN,
                    .found_type = TYPE_UNKNOWN
                };
                semantic_error_list_push(errors, err);
            } else {
                symtab_insert(table, node->as.var_decl.name, node->as.var_decl.decl_type, node->line);
            }
            break;
        }
        case NODE_ASSIGN: {
            SymbolEntry *entry = symtab_lookup(table, node->as.assign.name);
            DataType value_type = compute_expr_type(node->as.assign.value, table, errors);
            if (!entry) {
                SemanticError err = {
                    .kind = SEM_ERR_UNDECLARED,
                    .name = dup_str(node->as.assign.name),
                    .line = node->line,
                    .expected_type = TYPE_UNKNOWN,
                    .found_type = TYPE_UNKNOWN
                };
                semantic_error_list_push(errors, err);
            } else if (value_type != TYPE_UNKNOWN && entry->type == TYPE_INT && value_type == TYPE_FLOAT) {
                /* narrowing float -> int is flagged; int -> float widening is not */
                SemanticError err = {
                    .kind = SEM_ERR_TYPE_MISMATCH,
                    .name = dup_str(node->as.assign.name),
                    .line = node->line,
                    .expected_type = entry->type,
                    .found_type = value_type
                };
                semantic_error_list_push(errors, err);
            }
            break;
        }
        case NODE_IF:
            compute_expr_type(node->as.if_stmt.cond, table, errors);
            analyze_stmt(node->as.if_stmt.then_branch, table, errors);
            analyze_stmt(node->as.if_stmt.else_branch, table, errors);
            break;
        case NODE_WHILE:
            compute_expr_type(node->as.while_stmt.cond, table, errors);
            analyze_stmt(node->as.while_stmt.body, table, errors);
            break;
        case NODE_BLOCK:
            for (size_t i = 0; i < node->as.block.statements->count; i++)
                analyze_stmt(node->as.block.statements->items[i], table, errors);
            break;
        default:
            break; /* expression nodes never appear directly as statements */
    }
}

SymbolTable *analyze(AstNode *program, SemanticErrorList *errors_out) {
    SymbolTable *table = symtab_create();
    for (size_t i = 0; i < program->as.program.statements->count; i++) {
        analyze_stmt(program->as.program.statements->items[i], table, errors_out);
    }
    return table;
}
