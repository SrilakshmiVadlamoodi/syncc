#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_UNKNOWN   /* unresolved until semantic analysis */
} DataType;

typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,      /* int x; */
    NODE_ASSIGN,        /* x = expr; */
    NODE_IF,            /* if (cond) then_block [else else_block] */
    NODE_WHILE,         /* while (cond) body */
    NODE_BLOCK,         /* { stmt* } */
    NODE_BINOP,         /* expr op expr */
    NODE_IDENT,         /* variable reference */
    NODE_NUM_INT,
    NODE_NUM_FLOAT
} NodeType;

typedef struct AstNode AstNode;

/* Simple growable array of AstNode*, used for statement lists / blocks. */
typedef struct {
    AstNode **items;
    size_t count;
    size_t capacity;
} AstList;

struct AstNode {
    NodeType type;
    int line;
    int col;

    union {
        struct { AstList *statements; } program;

        struct { DataType decl_type; char *name; } var_decl;

        struct { char *name; AstNode *value; } assign;

        struct { AstNode *cond; AstNode *then_branch; AstNode *else_branch; /* NULL if no else */ } if_stmt;

        struct { AstNode *cond; AstNode *body; } while_stmt;

        struct { AstList *statements; } block;

        struct { TokenType op; AstNode *left; AstNode *right; } binop;

        struct { char *name; } ident;

        struct { int value; } num_int;

        struct { double value; } num_float;
    } as;
};

/* --- AST construction / teardown --- */
AstList *astlist_create(void);
void astlist_push(AstList *list, AstNode *node);
void astlist_free(AstList *list); /* frees contained nodes too */

AstNode *ast_free(AstNode *node); /* recursively frees node and children; returns NULL */

/* --- Parser --- */

typedef struct {
    TokenList *tokens;
    size_t pos;          /* index of current token */
} Parser;

/* Structured parse error, collected (not thrown) so the driver can gather
 * multiple errors per run via panic-mode recovery. */
typedef struct {
    TokenType expected;   /* token type the grammar rule wanted */
    Token found;          /* the actual token encountered */
    int line;
} ParseError;

typedef struct {
    ParseError *items;
    size_t count;
    size_t capacity;
} ParseErrorList;

ParseErrorList *parse_error_list_create(void);
void parse_error_list_push(ParseErrorList *list, ParseError err);
void parse_error_list_free(ParseErrorList *list);

/* Parses the full token stream into a NODE_PROGRAM AST root.
 * Never returns NULL: on malformed input it performs panic-mode recovery
 * (skips to the next ';' or '}') and keeps going, appending each failure
 * to errors_out. Caller owns both the returned AST and errors_out. */
AstNode *parse_program(TokenList *tokens, ParseErrorList *errors_out);

/* Indented recursive print of the AST, for the driver's "stage 2" output. */
void ast_print(AstNode *node, int indent);

#endif
