#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>

#include "lexer.h"
#include "parser.h"
#include "tacutil.h"

FrontEnd *frontend_run(const char *source) {
    FrontEnd *fe = calloc(1, sizeof(FrontEnd));

    TokenList *tokens = lex(source);
    codegen_reset_counters();
    ParseErrorList *parse_errors = parse_error_list_create();
    AstNode *ast = parse_program(tokens, parse_errors);

    SemanticErrorList *semantic_errors = semantic_error_list_create();
    fe->symtab = analyze(ast, semantic_errors);
    fe->tac = generate_tac(ast);
    fe->parse_errors = (int)parse_errors->count;
    fe->semantic_errors = (int)semantic_errors->count;

    semantic_error_list_free(semantic_errors);
    parse_error_list_free(parse_errors);
    ast_free(ast);
    tokenlist_free(tokens);
    return fe;
}

void frontend_free(FrontEnd *fe) {
    if (!fe) return;
    taclist_free(fe->tac);
    symtab_free(fe->symtab);
    free(fe);
}

char *pipeline_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int frontend_has_temp_collision(const SymbolTable *symtab) {
    for (size_t i = 0; i < symtab->count; i++)
        if (tac_is_temp_name(symtab->items[i].name)) return 1;
    return 0;
}
