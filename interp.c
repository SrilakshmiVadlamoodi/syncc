#include "interp.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "cost.h"
#include "strmap.h"
#include "tacutil.h"

/* ---------- values and environments ---------- */

Value value_int(long i) { return (Value){ .kind = VAL_INT, .i = i, .f = 0.0 }; }
Value value_float(double f) { return (Value){ .kind = VAL_FLOAT, .i = 0, .f = f }; }

struct Env {
    StrMap *map;
    Value *vals;
    size_t cap;
};

Env *env_create(void) {
    Env *e = malloc(sizeof(Env));
    e->map = strmap_create();
    e->cap = 16;
    e->vals = malloc(sizeof(Value) * e->cap);
    return e;
}

void env_set(Env *e, const char *name, Value v) {
    int idx = strmap_intern(e->map, name);
    while ((size_t)idx >= e->cap) {
        e->cap *= 2;
        e->vals = realloc(e->vals, sizeof(Value) * e->cap);
    }
    e->vals[idx] = v;
}

int env_get(const Env *e, const char *name, Value *out) {
    int idx = strmap_find(e->map, name);
    if (idx < 0) return 0;
    if (out) *out = e->vals[idx];
    return 1;
}

size_t env_size(const Env *e) { return strmap_count(e->map); }
const char *env_name_at(const Env *e, size_t i) { return strmap_key(e->map, (int)i); }
Value env_value_at(const Env *e, size_t i) { return e->vals[i]; }

Env *env_copy(const Env *e) {
    Env *c = env_create();
    for (size_t i = 0; i < env_size(e); i++)
        env_set(c, env_name_at(e, i), env_value_at(e, i));
    return c;
}

void env_free(Env *e) {
    if (!e) return;
    strmap_free(e->map);
    free(e->vals);
    free(e);
}

/* ---------- arithmetic (kept consistent with fold_binop) ---------- */

static int is_comparison(TokenType op) {
    return op == TOK_EQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE;
}

static double as_double(Value v) { return v.kind == VAL_FLOAT ? v.f : (double)v.i; }

static Value eval_binop(TokenType op, Value a, Value b, int *div_zero) {
    *div_zero = 0;
    int af = a.kind == VAL_FLOAT, bf = b.kind == VAL_FLOAT;

    if (is_comparison(op)) {
        int r = 0;
        if (!af && !bf) {
            long x = a.i, y = b.i;
            switch (op) {
                case TOK_EQ: r = x == y; break;
                case TOK_LT: r = x < y; break;
                case TOK_GT: r = x > y; break;
                case TOK_LE: r = x <= y; break;
                default:     r = x >= y; break;
            }
        } else {
            double x = as_double(a), y = as_double(b);
            switch (op) {
                case TOK_EQ: r = x == y; break;
                case TOK_LT: r = x < y; break;
                case TOK_GT: r = x > y; break;
                case TOK_LE: r = x <= y; break;
                default:     r = x >= y; break;
            }
        }
        return value_int(r);
    }

    if (af || bf) {
        double x = as_double(a), y = as_double(b), r = 0.0;
        switch (op) {
            case TOK_PLUS:  r = x + y; break;
            case TOK_MINUS: r = x - y; break;
            case TOK_STAR:  r = x * y; break;
            case TOK_SLASH:
                if (y == 0.0) { *div_zero = 1; return value_float(0.0); }
                r = x / y;
                break;
            default: break;
        }
        return value_float(r);
    }

    unsigned long ux = (unsigned long)a.i, uy = (unsigned long)b.i;
    switch (op) {
        case TOK_PLUS:  return value_int((long)(ux + uy));
        case TOK_MINUS: return value_int((long)(ux - uy));
        case TOK_STAR:  return value_int((long)(ux * uy));
        case TOK_SLASH:
            if (b.i == 0) { *div_zero = 1; return value_int(0); }
            if (a.i == LONG_MIN && b.i == -1) return value_int(LONG_MIN);
            return value_int(a.i / b.i);
        default: return value_int(0);
    }
}

static int is_zero(Value v) {
    return v.kind == VAL_FLOAT ? v.f == 0.0 : v.i == 0;
}

/* ---------- execution ---------- */

typedef struct { int is_lit; Value lit; int slot; } Opnd;

static Opnd make_opnd(const char *s, StrMap *slots) {
    Opnd o = { 0, value_int(0), 0 };
    if (!s) { o.is_lit = 1; return o; }
    if (tac_is_numeric_literal(s)) {
        o.is_lit = 1;
        o.lit = tac_is_float_literal(s) ? value_float(strtod(s, NULL))
                                        : value_int(strtol(s, NULL, 10));
    } else {
        o.slot = strmap_intern(slots, s);
    }
    return o;
}

static Value read_opnd(const Opnd *o, const Value *vals) {
    return o->is_lit ? o->lit : vals[o->slot];
}

InterpResult interp_run(const TACList *list, const Env *initial, long step_limit) {
    size_t n = list->count;
    StrMap *slots = strmap_create();
    StrMap *labels = strmap_create();

    Opnd *a1 = malloc(sizeof(Opnd) * (n + 1));
    Opnd *a2 = malloc(sizeof(Opnd) * (n + 1));
    int *res = malloc(sizeof(int) * (n + 1));
    size_t *target = malloc(sizeof(size_t) * (n + 1));
    size_t *label_pos = malloc(sizeof(size_t) * (n + 1));

    for (size_t i = 0; i < n; i++) {
        const TACInstr *in = &list->items[i];
        a1[i] = make_opnd(in->op == TAC_BINOP || in->op == TAC_ASSIGN || in->op == TAC_IF_FALSE_GOTO
                              ? in->arg1 : NULL, slots);
        a2[i] = make_opnd(in->op == TAC_BINOP ? in->arg2 : NULL, slots);
        res[i] = (in->op == TAC_ASSIGN || in->op == TAC_BINOP) ? strmap_intern(slots, in->result) : -1;
        if (in->op == TAC_LABEL) {
            int before = (int)strmap_count(labels);
            int id = strmap_intern(labels, in->result);
            if (id == before) label_pos[id] = i;
        }
    }
    for (size_t i = 0; i < n; i++) {
        const TACInstr *in = &list->items[i];
        target[i] = n;
        if (in->op == TAC_GOTO || in->op == TAC_IF_FALSE_GOTO) {
            int id = strmap_find(labels, in->result);
            if (id >= 0) target[i] = label_pos[id];
        }
    }

    size_t nslots = strmap_count(slots);
    Value *vals = malloc(sizeof(Value) * (nslots + 1));
    for (size_t s = 0; s < nslots; s++) vals[s] = value_int(0);
    if (initial) {
        for (size_t k = 0; k < env_size(initial); k++) {
            int s = strmap_find(slots, env_name_at(initial, k));
            if (s >= 0) vals[s] = env_value_at(initial, k);
        }
    }

    InterpResult out = { INTERP_OK, 0, 0, NULL };
    size_t pc = 0;
    while (pc < n) {
        if (out.steps >= step_limit) { out.status = INTERP_STEP_LIMIT; break; }
        out.steps++;
        const TACInstr *in = &list->items[pc];
        out.dynamic_cost += cost_of_instr(in);
        switch (in->op) {
            case TAC_ASSIGN:
                vals[res[pc]] = read_opnd(&a1[pc], vals);
                pc++;
                break;
            case TAC_BINOP: {
                int dz = 0;
                Value v = eval_binop(in->binop, read_opnd(&a1[pc], vals), read_opnd(&a2[pc], vals), &dz);
                if (dz) { out.status = INTERP_DIV_ZERO; pc = n; break; }
                vals[res[pc]] = v;
                pc++;
                break;
            }
            case TAC_LABEL:
                pc++;
                break;
            case TAC_GOTO:
                pc = target[pc];
                break;
            case TAC_IF_FALSE_GOTO:
                pc = is_zero(read_opnd(&a1[pc], vals)) ? target[pc] : pc + 1;
                break;
        }
    }

    out.final_env = initial ? env_copy(initial) : env_create();
    for (size_t s = 0; s < nslots; s++)
        env_set(out.final_env, strmap_key(slots, (int)s), vals[s]);

    free(vals);
    free(label_pos);
    free(target);
    free(res);
    free(a2);
    free(a1);
    strmap_free(labels);
    strmap_free(slots);
    return out;
}

void interp_result_free(InterpResult *r) {
    env_free(r->final_env);
    r->final_env = NULL;
}

int interp_equivalent(const InterpResult *a, const InterpResult *b,
                      const SymbolTable *user_vars) {
    if (a->status != INTERP_OK || b->status != INTERP_OK) return 0;
    for (size_t i = 0; i < user_vars->count; i++) {
        const char *name = user_vars->items[i].name;
        Value va = value_int(0), vb = value_int(0);
        env_get(a->final_env, name, &va);
        env_get(b->final_env, name, &vb);
        if (va.kind != vb.kind) return 0;
        if (va.kind == VAL_INT) {
            if (va.i != vb.i) return 0;
        } else if (va.f != vb.f) {
            if (!(va.f != va.f && vb.f != vb.f)) return 0;   /* both NaN is equal */
        }
    }
    return 1;
}

long interp_dynamic_cost(const TACList *list, const Env *initial) {
    InterpResult r = interp_run(list, initial, INTERP_DEFAULT_STEP_LIMIT);
    long cost = r.status == INTERP_OK ? r.dynamic_cost : -1;
    interp_result_free(&r);
    return cost;
}
