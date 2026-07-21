#ifndef ERROR_ENGINE_H
#define ERROR_ENGINE_H

#include "lexer.h"
#include "parser.h"
#include "semantic.h"

/* --- Rule-based suggestions (always available, zero config) --- */

typedef struct {
    TokenType expected;
    TokenType found;
    const char *message;   /* static string, not owned/freed */
} ErrorRule;

/* Looks up (err->expected, err->found.type) in the static rule table.
 * Returns a suggestion string on match, or the default
 * "Syntax error near line X" fallback (heap-allocated; caller frees)
 * if nothing matches. Never fails/returns NULL. */
char *error_engine_suggest_parse(const ParseError *err);

/* Same idea for semantic errors (redeclaration / undeclared /
 * type mismatch), phrased from the SemanticError's kind + names.
 * Heap-allocated; caller frees. */
char *error_engine_suggest_semantic(const SemanticError *err);

/* --- Optional LLM fallback (disabled unless an API key is configured) --- */

/* Attempts a Claude Haiku call with only {expected, found, offending
 * source line} as context (max_tokens ~60). Returns a heap-allocated
 * suggestion string on success. Returns NULL if ANTHROPIC_API_KEY is
 * unset, the request fails, or times out -- callers must treat NULL as
 * "silently fall back to error_engine_suggest_parse", never as a hard
 * error. The whole project must run correctly with zero API key set. */
char *error_engine_try_llm_suggestion(const ParseError *err, const char *source_line);

#endif
