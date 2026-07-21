#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INITIAL_CAPACITY 64

TokenList *tokenlist_create(void) {
    TokenList *list = malloc(sizeof(TokenList));
    list->items = malloc(sizeof(Token) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

void tokenlist_push(TokenList *list, Token tok) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(Token) * list->capacity);
    }
    list->items[list->count++] = tok;
}

void tokenlist_free(TokenList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].lexeme);
    }
    free(list->items);
    free(list);
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_IDENT:      return "IDENT";
        case TOK_NUM_INT:    return "NUM_INT";
        case TOK_NUM_FLOAT:  return "NUM_FLOAT";
        case TOK_INT:        return "INT";
        case TOK_FLOAT:      return "FLOAT";
        case TOK_IF:         return "IF";
        case TOK_ELSE:       return "ELSE";
        case TOK_WHILE:      return "WHILE";
        case TOK_PLUS:       return "PLUS";
        case TOK_MINUS:      return "MINUS";
        case TOK_STAR:       return "STAR";
        case TOK_SLASH:      return "SLASH";
        case TOK_ASSIGN:     return "ASSIGN";
        case TOK_EQ:         return "EQ";
        case TOK_LT:         return "LT";
        case TOK_GT:         return "GT";
        case TOK_LE:         return "LE";
        case TOK_GE:         return "GE";
        case TOK_LPAREN:     return "LPAREN";
        case TOK_RPAREN:     return "RPAREN";
        case TOK_LBRACE:     return "LBRACE";
        case TOK_RBRACE:     return "RBRACE";
        case TOK_SEMI:       return "SEMI";
        case TOK_EOF:        return "EOF";
        case TOK_ERROR:      return "ERROR";
    }
    return "UNKNOWN";
}

/* --- scanning state --- */

typedef struct {
    const char *src;
    size_t pos;
    int line;
    int col;
} Scanner;

static char peek(Scanner *s) { return s->src[s->pos]; }
static char peek2(Scanner *s) { return s->src[s->pos] ? s->src[s->pos + 1] : '\0'; }

static char advance(Scanner *s) {
    char c = s->src[s->pos++];
    if (c == '\n') {
        s->line++;
        s->col = 1;
    } else {
        s->col++;
    }
    return c;
}

static char *dup_range(const char *start, size_t len) {
    char *out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static Token make_token(TokenType type, const char *text, int line, int col) {
    Token t;
    t.type = type;
    t.lexeme = dup_range(text, strlen(text));
    t.line = line;
    t.col = col;
    return t;
}

static void skip_whitespace_and_comments(Scanner *s) {
    for (;;) {
        char c = peek(s);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(s);
        } else if (c == '/' && peek2(s) == '/') {
            while (peek(s) && peek(s) != '\n') advance(s);
        } else if (c == '/' && peek2(s) == '*') {
            advance(s); advance(s); /* consume block-comment opener */
            while (peek(s) && !(peek(s) == '*' && peek2(s) == '/')) advance(s);
            if (peek(s)) { advance(s); advance(s); } /* consume closer */
        } else {
            break;
        }
    }
}

static TokenType keyword_or_ident(const char *text) {
    if (strcmp(text, "int") == 0) return TOK_INT;
    if (strcmp(text, "float") == 0) return TOK_FLOAT;
    if (strcmp(text, "if") == 0) return TOK_IF;
    if (strcmp(text, "else") == 0) return TOK_ELSE;
    if (strcmp(text, "while") == 0) return TOK_WHILE;
    return TOK_IDENT;
}

TokenList *lex(const char *source) {
    TokenList *tokens = tokenlist_create();
    Scanner s = { source, 0, 1, 1 };

    for (;;) {
        skip_whitespace_and_comments(&s);
        int line = s.line, col = s.col;
        char c = peek(&s);

        if (c == '\0') {
            tokenlist_push(tokens, make_token(TOK_EOF, "", line, col));
            break;
        }

        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = s.pos;
            while (isalnum((unsigned char)peek(&s)) || peek(&s) == '_') advance(&s);
            char *text = dup_range(source + start, s.pos - start);
            Token t = make_token(keyword_or_ident(text), text, line, col);
            free(text);
            tokenlist_push(tokens, t);
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t start = s.pos;
            int is_float = 0;
            while (isdigit((unsigned char)peek(&s))) advance(&s);
            if (peek(&s) == '.' && isdigit((unsigned char)peek2(&s))) {
                is_float = 1;
                advance(&s); /* consume '.' */
                while (isdigit((unsigned char)peek(&s))) advance(&s);
            }
            char *text = dup_range(source + start, s.pos - start);
            Token t = make_token(is_float ? TOK_NUM_FLOAT : TOK_NUM_INT, text, line, col);
            free(text);
            tokenlist_push(tokens, t);
            continue;
        }

        /* two-character operators first */
        if (c == '=' && peek2(&s) == '=') { advance(&s); advance(&s); tokenlist_push(tokens, make_token(TOK_EQ, "==", line, col)); continue; }
        if (c == '<' && peek2(&s) == '=') { advance(&s); advance(&s); tokenlist_push(tokens, make_token(TOK_LE, "<=", line, col)); continue; }
        if (c == '>' && peek2(&s) == '=') { advance(&s); advance(&s); tokenlist_push(tokens, make_token(TOK_GE, ">=", line, col)); continue; }

        switch (c) {
            case '+': advance(&s); tokenlist_push(tokens, make_token(TOK_PLUS, "+", line, col)); continue;
            case '-': advance(&s); tokenlist_push(tokens, make_token(TOK_MINUS, "-", line, col)); continue;
            case '*': advance(&s); tokenlist_push(tokens, make_token(TOK_STAR, "*", line, col)); continue;
            case '/': advance(&s); tokenlist_push(tokens, make_token(TOK_SLASH, "/", line, col)); continue;
            case '=': advance(&s); tokenlist_push(tokens, make_token(TOK_ASSIGN, "=", line, col)); continue;
            case '<': advance(&s); tokenlist_push(tokens, make_token(TOK_LT, "<", line, col)); continue;
            case '>': advance(&s); tokenlist_push(tokens, make_token(TOK_GT, ">", line, col)); continue;
            case '(': advance(&s); tokenlist_push(tokens, make_token(TOK_LPAREN, "(", line, col)); continue;
            case ')': advance(&s); tokenlist_push(tokens, make_token(TOK_RPAREN, ")", line, col)); continue;
            case '{': advance(&s); tokenlist_push(tokens, make_token(TOK_LBRACE, "{", line, col)); continue;
            case '}': advance(&s); tokenlist_push(tokens, make_token(TOK_RBRACE, "}", line, col)); continue;
            case ';': advance(&s); tokenlist_push(tokens, make_token(TOK_SEMI, ";", line, col)); continue;
        }

        /* unrecognized character: emit TOK_ERROR and keep scanning so the
         * rest of the file is still tokenized (feeds error_engine later) */
        advance(&s);
        char bad[2] = { c, '\0' };
        fprintf(stderr, "lexical error: unexpected character '%c' at line %d, col %d\n", c, line, col);
        tokenlist_push(tokens, make_token(TOK_ERROR, bad, line, col));
    }

    return tokens;
}
