#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

typedef enum {
    /* literals & identifiers */
    TOK_IDENT, TOK_NUM_INT, TOK_NUM_FLOAT,

    /* keywords */
    TOK_INT, TOK_FLOAT, TOK_IF, TOK_ELSE, TOK_WHILE,

    /* operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH,
    TOK_ASSIGN, TOK_EQ, TOK_LT, TOK_GT, TOK_LE, TOK_GE,

    /* punctuation */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_SEMI,

    TOK_EOF,
    TOK_ERROR   /* invalid character; value holds the offending char */
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;   /* heap-allocated copy of the raw text; owned by Token */
    int line;
    int col;
} Token;

/* Dynamic array of tokens, produced in one pass by lex(). */
typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

TokenList *tokenlist_create(void);
void tokenlist_push(TokenList *list, Token tok);
void tokenlist_free(TokenList *list);

/* Runs the full source through the lexer and returns a populated TokenList.
 * Lexical errors (bad characters) are emitted as TOK_ERROR tokens inline
 * (also reported via error_engine) rather than aborting the scan, so the
 * rest of the file is still tokenized. */
TokenList *lex(const char *source);

const char *token_type_name(TokenType type);

#endif
