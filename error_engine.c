#include "error_engine.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

static char *format_message(const char *fmt, ...) {
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    char *buf = malloc((size_t)len + 1);
    vsnprintf(buf, (size_t)len + 1, fmt, args);
    va_end(args);
    return buf;
}

/* ================= rule-based parse-error suggestions ================= */

static const ErrorRule RULE_TABLE[] = {
    { TOK_SEMI,   TOK_RBRACE, "Missing semicolon on the previous line" },
    { TOK_SEMI,   TOK_EOF,    "Missing semicolon before end of file" },
    { TOK_RPAREN, TOK_EOF,    "Unclosed parenthesis" },
    { TOK_RPAREN, TOK_SEMI,   "Unclosed parenthesis before ';'" },
    { TOK_RBRACE, TOK_EOF,    "Missing closing brace '}'" },
    { TOK_ASSIGN, TOK_SEMI,   "Expected '=' in assignment" },
    { TOK_IDENT,  TOK_INT,    "Can't use a reserved word as a variable name" },
    { TOK_IDENT,  TOK_FLOAT,  "Can't use a reserved word as a variable name" },
    { TOK_IDENT,  TOK_IF,     "Can't use a reserved word as a variable name" },
    { TOK_IDENT,  TOK_ELSE,   "Can't use a reserved word as a variable name" },
    { TOK_IDENT,  TOK_WHILE,  "Can't use a reserved word as a variable name" },
};
#define RULE_TABLE_SIZE (sizeof(RULE_TABLE) / sizeof(RULE_TABLE[0]))

char *error_engine_suggest_parse(const ParseError *err) {
    for (size_t i = 0; i < RULE_TABLE_SIZE; i++) {
        if (RULE_TABLE[i].expected == err->expected && RULE_TABLE[i].found == err->found.type) {
            return format_message("%s", RULE_TABLE[i].message);
        }
    }
    return format_message("Syntax error near line %d", err->line);
}

/* ================= semantic-error suggestions ================= */

static const char *type_name(DataType type) {
    switch (type) {
        case TYPE_INT:   return "int";
        case TYPE_FLOAT: return "float";
        default:         return "unknown";
    }
}

char *error_engine_suggest_semantic(const SemanticError *err) {
    switch (err->kind) {
        case SEM_ERR_REDECLARATION:
            return format_message("Variable '%s' is already declared (redeclaration)", err->name);
        case SEM_ERR_UNDECLARED:
            return format_message("Variable '%s' used before it was declared", err->name);
        case SEM_ERR_TYPE_MISMATCH:
            return format_message(
                "Type mismatch: cannot assign a %s value to '%s' declared as %s",
                type_name(err->found_type), err->name, type_name(err->expected_type));
        default:
            return format_message("Semantic error near line %d", err->line);
    }
}

/* ================= optional LLM fallback (stubbed) ================= */

char *error_engine_try_llm_suggestion(const ParseError *err, const char *source_line) {
    (void)err;
    (void)source_line;
    /* Out of scope for now: a live call here would read ANTHROPIC_API_KEY,
     * POST {expected, found, source_line} to the Claude Haiku messages
     * endpoint with max_tokens ~60, and return NULL on any failure so the
     * caller falls back to error_engine_suggest_parse(). Returning NULL
     * unconditionally reproduces that "no key configured" fallback path. */
    return NULL;
}
