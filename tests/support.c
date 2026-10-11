#include "support.h"

#include <stdlib.h>
#include <string.h>

#include "tacutil.h"

int g_checks = 0;
int g_failures = 0;

const char *SAMPLE_FILES[] = {
    "samples/valid.mini",
    "samples/constant_folding.mini",
    "samples/loop_invariant.mini",
    "samples/loop_invariant_chain.mini",
    "samples/loop_no_invariant.mini",
    "samples/loop_with_if.mini",
    NULL
};

static int split_ws(char *line, char **tok, int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static TokenType op_from_symbol(const char *s) {
    if (!strcmp(s, "+")) return TOK_PLUS;
    if (!strcmp(s, "-")) return TOK_MINUS;
    if (!strcmp(s, "*")) return TOK_STAR;
    if (!strcmp(s, "/")) return TOK_SLASH;
    if (!strcmp(s, "==")) return TOK_EQ;
    if (!strcmp(s, "<")) return TOK_LT;
    if (!strcmp(s, ">")) return TOK_GT;
    if (!strcmp(s, "<=")) return TOK_LE;
    if (!strcmp(s, ">=")) return TOK_GE;
    return TOK_ERROR;
}

TACList *tac_from_text(const char *text) {
    TACList *list = taclist_create();
    char *copy = tac_dup_str(text);
    char *line = copy;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        char *tok[8];
        int n = split_ws(line, tok, 8);
        size_t len0 = n > 0 ? strlen(tok[0]) : 0;
        if (n == 1 && tok[0][len0 - 1] == ':') {
            tok[0][len0 - 1] = '\0';
            taclist_push(list, (TACInstr){ .op = TAC_LABEL, .result = tac_dup_str(tok[0]) });
        } else if (n == 2 && !strcmp(tok[0], "goto")) {
            taclist_push(list, (TACInstr){ .op = TAC_GOTO, .result = tac_dup_str(tok[1]) });
        } else if (n == 4 && !strcmp(tok[0], "ifFalse") && !strcmp(tok[2], "goto")) {
            taclist_push(list, (TACInstr){ .op = TAC_IF_FALSE_GOTO,
                .result = tac_dup_str(tok[3]), .arg1 = tac_dup_str(tok[1]) });
        } else if (n == 3 && !strcmp(tok[1], "=")) {
            taclist_push(list, (TACInstr){ .op = TAC_ASSIGN,
                .result = tac_dup_str(tok[0]), .arg1 = tac_dup_str(tok[2]) });
        } else if (n == 5 && !strcmp(tok[1], "=")) {
            taclist_push(list, (TACInstr){ .op = TAC_BINOP,
                .result = tac_dup_str(tok[0]), .arg1 = tac_dup_str(tok[2]),
                .arg2 = tac_dup_str(tok[4]), .binop = op_from_symbol(tok[3]) });
        } else if (n != 0) {
            fprintf(stderr, "tac_from_text: bad line: %s\n", line);
            abort();
        }
        if (!eol) break;
        line = eol + 1;
    }
    free(copy);
    return list;
}

SymbolTable *symtab_of(const char *names) {
    SymbolTable *t = symtab_create();
    char buf[512];
    strncpy(buf, names, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = strtok(buf, " "); p; p = strtok(NULL, " "))
        symtab_insert(t, p, TYPE_INT, 0);
    return t;
}

#define CAPTURE_PATH "tests_tmp.out"

FILE *capture_open(void) {
    return fopen(CAPTURE_PATH, "w+b");
}

char *capture_close(FILE *f) {
    fflush(f);
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    size_t got = fread(s, 1, (size_t)n, f);
    s[got] = '\0';
    fclose(f);
    remove(CAPTURE_PATH);
    return s;
}
