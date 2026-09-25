#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"
#include "optimizer.h"
#include "error_engine.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: could not open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

/* Returns the 1-indexed `line` from `source`, or "" if out of range.
 * Heap-allocated; caller frees. Used to give the error engine (and any
 * future LLM fallback) just the single offending source line. */
static char *extract_line(const char *source, int line) {
    int current = 1;
    const char *start = source;
    while (current < line && *start) {
        if (*start == '\n') current++;
        start++;
    }
    const char *end = start;
    while (*end && *end != '\n') end++;

    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <source.mini>\n", argv[0]);
        return 1;
    }

    char *source = read_file(argv[1]);

    /* --- Stage 1: Lexing --- */
    TokenList *tokens = lex(source);
    printf("=== Tokens ===\n");
    for (size_t i = 0; i < tokens->count; i++) {
        Token *t = &tokens->items[i];
        printf("  %-12s '%s'  (line %d, col %d)\n",
               token_type_name(t->type), t->lexeme, t->line, t->col);
    }

    /* --- Stage 2: Parsing --- */
    codegen_reset_counters();
    ParseErrorList *parse_errors = parse_error_list_create();
    AstNode *ast = parse_program(tokens, parse_errors);

    printf("\n=== AST ===\n");
    ast_print(ast, 0);

    /* --- Stage 3: Semantic analysis --- */
    SemanticErrorList *semantic_errors = semantic_error_list_create();
    SymbolTable *symtab = analyze(ast, semantic_errors);

    printf("\n=== Symbol Table ===\n");
    symtab_print(symtab, stdout);

    /* --- Stage 4: TAC generation --- */
    TACList *tac = generate_tac(ast);
    printf("\n=== TAC (unoptimized) ===\n");
    tac_print(tac, stdout);

    /* --- Stage 5: Optimization (constant folding, then loop-invariant
     * code motion over the folded TAC) --- */
    TACList *tac_opt = optimize_constant_folding(tac);
    printf("\n=== TAC (optimized: constant folding) ===\n");
    tac_print(tac_opt, stdout);

    TACList *tac_licm = optimize_loop_invariant_code_motion(tac_opt);
    printf("\n=== TAC (optimized: + loop-invariant code motion) ===\n");
    tac_print(tac_licm, stdout);

    /* --- Stage 6: Errors + suggestions --- */
    if (parse_errors->count > 0 || semantic_errors->count > 0) {
        printf("\n=== Errors ===\n");
        for (size_t i = 0; i < parse_errors->count; i++) {
            ParseError *e = &parse_errors->items[i];
            char *line_text = extract_line(source, e->line);

            char *suggestion = error_engine_try_llm_suggestion(e, line_text);
            if (!suggestion) {
                suggestion = error_engine_suggest_parse(e);
            }
            printf("  [Parse]    line %d: %s\n", e->line, suggestion);

            free(suggestion);
            free(line_text);
        }
        for (size_t i = 0; i < semantic_errors->count; i++) {
            SemanticError *e = &semantic_errors->items[i];
            char *suggestion = error_engine_suggest_semantic(e);
            printf("  [Semantic] line %d: %s\n", e->line, suggestion);
            free(suggestion);
        }
    }

    /* --- Cleanup --- */
    taclist_free(tac_licm);
    taclist_free(tac_opt);
    taclist_free(tac);
    symtab_free(symtab);
    semantic_error_list_free(semantic_errors);
    ast_free(ast);
    parse_error_list_free(parse_errors);
    tokenlist_free(tokens);
    free(source);

    return 0;
}
