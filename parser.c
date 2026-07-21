#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

/* ================= AstList ================= */

AstList *astlist_create(void) {
    AstList *list = malloc(sizeof(AstList));
    list->items = malloc(sizeof(AstNode *) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

void astlist_push(AstList *list, AstNode *node) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(AstNode *) * list->capacity);
    }
    list->items[list->count++] = node;
}

void astlist_free(AstList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        ast_free(list->items[i]);
    }
    free(list->items);
    free(list);
}

/* ================= ParseErrorList ================= */

ParseErrorList *parse_error_list_create(void) {
    ParseErrorList *list = malloc(sizeof(ParseErrorList));
    list->items = malloc(sizeof(ParseError) * INITIAL_CAPACITY);
    list->count = 0;
    list->capacity = INITIAL_CAPACITY;
    return list;
}

void parse_error_list_push(ParseErrorList *list, ParseError err) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, sizeof(ParseError) * list->capacity);
    }
    list->items[list->count++] = err;
}

void parse_error_list_free(ParseErrorList *list) {
    if (!list) return;
    /* ParseError.found.lexeme is a borrowed pointer (owned by the
     * TokenList the parser read from), so nothing to free per-entry. */
    free(list->items);
    free(list);
}

/* ================= AST node helpers ================= */

static AstNode *new_node(NodeType type, int line, int col) {
    AstNode *node = malloc(sizeof(AstNode));
    node->type = type;
    node->line = line;
    node->col = col;
    return node;
}

static char *dup_str(const char *s) {
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

AstNode *ast_free(AstNode *node) {
    if (!node) return NULL;
    switch (node->type) {
        case NODE_PROGRAM:
            astlist_free(node->as.program.statements);
            break;
        case NODE_VAR_DECL:
            free(node->as.var_decl.name);
            break;
        case NODE_ASSIGN:
            free(node->as.assign.name);
            ast_free(node->as.assign.value);
            break;
        case NODE_IF:
            ast_free(node->as.if_stmt.cond);
            ast_free(node->as.if_stmt.then_branch);
            ast_free(node->as.if_stmt.else_branch);
            break;
        case NODE_WHILE:
            ast_free(node->as.while_stmt.cond);
            ast_free(node->as.while_stmt.body);
            break;
        case NODE_BLOCK:
            astlist_free(node->as.block.statements);
            break;
        case NODE_BINOP:
            ast_free(node->as.binop.left);
            ast_free(node->as.binop.right);
            break;
        case NODE_IDENT:
            free(node->as.ident.name);
            break;
        case NODE_NUM_INT:
        case NODE_NUM_FLOAT:
            break;
    }
    free(node);
    return NULL;
}

/* ================= AST print ================= */

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static const char *binop_symbol(TokenType op) {
    switch (op) {
        case TOK_PLUS:  return "+";
        case TOK_MINUS: return "-";
        case TOK_STAR:  return "*";
        case TOK_SLASH: return "/";
        case TOK_EQ:    return "==";
        case TOK_LT:    return "<";
        case TOK_GT:    return ">";
        case TOK_LE:    return "<=";
        case TOK_GE:    return ">=";
        default:        return "?";
    }
}

void ast_print(AstNode *node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("<null>\n");
        return;
    }

    print_indent(indent);
    switch (node->type) {
        case NODE_PROGRAM:
            printf("Program\n");
            for (size_t i = 0; i < node->as.program.statements->count; i++)
                ast_print(node->as.program.statements->items[i], indent + 1);
            break;
        case NODE_VAR_DECL:
            printf("VarDecl %s : %s\n", node->as.var_decl.name,
                   node->as.var_decl.decl_type == TYPE_INT ? "int" : "float");
            break;
        case NODE_ASSIGN:
            printf("Assign %s =\n", node->as.assign.name);
            ast_print(node->as.assign.value, indent + 1);
            break;
        case NODE_IF:
            printf("If\n");
            print_indent(indent + 1); printf("cond:\n");
            ast_print(node->as.if_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("then:\n");
            ast_print(node->as.if_stmt.then_branch, indent + 2);
            if (node->as.if_stmt.else_branch) {
                print_indent(indent + 1); printf("else:\n");
                ast_print(node->as.if_stmt.else_branch, indent + 2);
            }
            break;
        case NODE_WHILE:
            printf("While\n");
            print_indent(indent + 1); printf("cond:\n");
            ast_print(node->as.while_stmt.cond, indent + 2);
            print_indent(indent + 1); printf("body:\n");
            ast_print(node->as.while_stmt.body, indent + 2);
            break;
        case NODE_BLOCK:
            printf("Block\n");
            for (size_t i = 0; i < node->as.block.statements->count; i++)
                ast_print(node->as.block.statements->items[i], indent + 1);
            break;
        case NODE_BINOP:
            printf("BinOp %s\n", binop_symbol(node->as.binop.op));
            ast_print(node->as.binop.left, indent + 1);
            ast_print(node->as.binop.right, indent + 1);
            break;
        case NODE_IDENT:
            printf("Ident %s\n", node->as.ident.name);
            break;
        case NODE_NUM_INT:
            printf("Int %d\n", node->as.num_int.value);
            break;
        case NODE_NUM_FLOAT:
            printf("Float %g\n", node->as.num_float.value);
            break;
    }
}

/* ================= Parser ================= */

static Token *cur(Parser *p) {
    return &p->tokens->items[p->pos];
}

static int check(Parser *p, TokenType type) {
    return cur(p)->type == type;
}

static Token advance_tok(Parser *p) {
    Token t = *cur(p);
    if (cur(p)->type != TOK_EOF) p->pos++;
    return t;
}

/* Skips tokens until the next ';' (consumed) or '}'/EOF (left for the
 * caller, e.g. block() needs to see the '}' to stop). This is the
 * panic-mode recovery the spec calls for. */
static void synchronize(Parser *p) {
    while (!check(p, TOK_SEMI) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        advance_tok(p);
    }
    if (check(p, TOK_SEMI)) advance_tok(p);
}

/* Consumes a token of `type`, or records a ParseError and synchronizes.
 * Always returns a Token safe to read (lexeme borrowed, never owned by
 * the caller) so expression/statement builders can proceed without NULL
 * checks. */
static Token expect(Parser *p, TokenType type, ParseErrorList *errors) {
    if (check(p, type)) return advance_tok(p);

    Token found = *cur(p);
    ParseError err = { .expected = type, .found = found, .line = found.line };
    parse_error_list_push(errors, err);
    synchronize(p);

    Token dummy;
    dummy.type = type;
    dummy.lexeme = "";
    dummy.line = found.line;
    dummy.col = found.col;
    return dummy;
}

static AstNode *parse_expr(Parser *p, ParseErrorList *errors);

static AstNode *parse_primary(Parser *p, ParseErrorList *errors) {
    Token t = *cur(p);

    if (t.type == TOK_NUM_INT) {
        advance_tok(p);
        AstNode *n = new_node(NODE_NUM_INT, t.line, t.col);
        n->as.num_int.value = atoi(t.lexeme);
        return n;
    }
    if (t.type == TOK_NUM_FLOAT) {
        advance_tok(p);
        AstNode *n = new_node(NODE_NUM_FLOAT, t.line, t.col);
        n->as.num_float.value = atof(t.lexeme);
        return n;
    }
    if (t.type == TOK_IDENT) {
        advance_tok(p);
        AstNode *n = new_node(NODE_IDENT, t.line, t.col);
        n->as.ident.name = dup_str(t.lexeme);
        return n;
    }
    if (t.type == TOK_LPAREN) {
        advance_tok(p);
        AstNode *inner = parse_expr(p, errors);
        expect(p, TOK_RPAREN, errors);
        return inner;
    }

    /* Nothing here looks like an expression: record it and fabricate a
     * placeholder node (0) so the caller always gets a valid AstNode*. */
    ParseError err = { .expected = TOK_IDENT, .found = t, .line = t.line };
    parse_error_list_push(errors, err);
    synchronize(p);
    AstNode *n = new_node(NODE_NUM_INT, t.line, t.col);
    n->as.num_int.value = 0;
    return n;
}

static AstNode *parse_multiplicative(Parser *p, ParseErrorList *errors) {
    AstNode *left = parse_primary(p, errors);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH)) {
        Token op = advance_tok(p);
        AstNode *right = parse_primary(p, errors);
        AstNode *n = new_node(NODE_BINOP, op.line, op.col);
        n->as.binop.op = op.type;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

static AstNode *parse_additive(Parser *p, ParseErrorList *errors) {
    AstNode *left = parse_multiplicative(p, errors);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token op = advance_tok(p);
        AstNode *right = parse_multiplicative(p, errors);
        AstNode *n = new_node(NODE_BINOP, op.line, op.col);
        n->as.binop.op = op.type;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

static int is_comparison_op(TokenType t) {
    return t == TOK_EQ || t == TOK_LT || t == TOK_GT || t == TOK_LE || t == TOK_GE;
}

static AstNode *parse_comparison(Parser *p, ParseErrorList *errors) {
    AstNode *left = parse_additive(p, errors);
    while (is_comparison_op(cur(p)->type)) {
        Token op = advance_tok(p);
        AstNode *right = parse_additive(p, errors);
        AstNode *n = new_node(NODE_BINOP, op.line, op.col);
        n->as.binop.op = op.type;
        n->as.binop.left = left;
        n->as.binop.right = right;
        left = n;
    }
    return left;
}

static AstNode *parse_expr(Parser *p, ParseErrorList *errors) {
    return parse_comparison(p, errors);
}

static AstNode *parse_statement(Parser *p, ParseErrorList *errors);

static AstNode *parse_block(Parser *p, ParseErrorList *errors) {
    Token open = expect(p, TOK_LBRACE, errors);
    AstNode *node = new_node(NODE_BLOCK, open.line, open.col);
    node->as.block.statements = astlist_create();

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode *stmt = parse_statement(p, errors);
        if (stmt) astlist_push(node->as.block.statements, stmt);
    }
    expect(p, TOK_RBRACE, errors);
    return node;
}

static AstNode *parse_var_decl(Parser *p, ParseErrorList *errors) {
    Token type_tok = advance_tok(p); /* 'int' or 'float' */
    Token name_tok = expect(p, TOK_IDENT, errors);
    expect(p, TOK_SEMI, errors);

    AstNode *node = new_node(NODE_VAR_DECL, type_tok.line, type_tok.col);
    node->as.var_decl.decl_type = (type_tok.type == TOK_INT) ? TYPE_INT : TYPE_FLOAT;
    node->as.var_decl.name = dup_str(name_tok.lexeme);
    return node;
}

static AstNode *parse_assign(Parser *p, ParseErrorList *errors) {
    Token name_tok = advance_tok(p); /* IDENT */
    expect(p, TOK_ASSIGN, errors);
    AstNode *value = parse_expr(p, errors);
    expect(p, TOK_SEMI, errors);

    AstNode *node = new_node(NODE_ASSIGN, name_tok.line, name_tok.col);
    node->as.assign.name = dup_str(name_tok.lexeme);
    node->as.assign.value = value;
    return node;
}

static AstNode *parse_if(Parser *p, ParseErrorList *errors) {
    Token if_tok = advance_tok(p);
    expect(p, TOK_LPAREN, errors);
    AstNode *cond = parse_expr(p, errors);
    expect(p, TOK_RPAREN, errors);
    AstNode *then_branch = parse_statement(p, errors);

    AstNode *else_branch = NULL;
    if (check(p, TOK_ELSE)) {
        advance_tok(p);
        else_branch = parse_statement(p, errors);
    }

    AstNode *node = new_node(NODE_IF, if_tok.line, if_tok.col);
    node->as.if_stmt.cond = cond;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    return node;
}

static AstNode *parse_while(Parser *p, ParseErrorList *errors) {
    Token while_tok = advance_tok(p);
    expect(p, TOK_LPAREN, errors);
    AstNode *cond = parse_expr(p, errors);
    expect(p, TOK_RPAREN, errors);
    AstNode *body = parse_statement(p, errors);

    AstNode *node = new_node(NODE_WHILE, while_tok.line, while_tok.col);
    node->as.while_stmt.cond = cond;
    node->as.while_stmt.body = body;
    return node;
}

static AstNode *parse_statement(Parser *p, ParseErrorList *errors) {
    switch (cur(p)->type) {
        case TOK_INT:
        case TOK_FLOAT:
            return parse_var_decl(p, errors);
        case TOK_IDENT:
            return parse_assign(p, errors);
        case TOK_IF:
            return parse_if(p, errors);
        case TOK_WHILE:
            return parse_while(p, errors);
        case TOK_LBRACE:
            return parse_block(p, errors);
        default: {
            Token t = *cur(p);
            if (t.type == TOK_RBRACE || t.type == TOK_EOF) {
                /* Sentinel token, not a real statement start: an earlier
                 * expect() failure already reported the real problem and
                 * deliberately left this token unconsumed so an enclosing
                 * block()/parse_program() loop can use it to stop. Return
                 * without pushing another error or touching the position,
                 * so the caller's loop condition (which checks for these)
                 * is the one that terminates -- not another lap here. */
                return NULL;
            }
            ParseError err = { .expected = TOK_SEMI, .found = t, .line = t.line };
            parse_error_list_push(errors, err);
            synchronize(p);
            return NULL;
        }
    }
}

AstNode *parse_program(TokenList *tokens, ParseErrorList *errors_out) {
    Parser parser = { tokens, 0 };
    AstNode *program = new_node(NODE_PROGRAM, 1, 1);
    program->as.program.statements = astlist_create();

    while (!check(&parser, TOK_EOF) && !check(&parser, TOK_RBRACE)) {
        AstNode *stmt = parse_statement(&parser, errors_out);
        if (stmt) astlist_push(program->as.program.statements, stmt);
    }

    return program;
}
