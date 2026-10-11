#include "fuzz.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"

/* ---------- tiny RNG and string buffer ---------- */

typedef struct { unsigned s; } Rng;

static unsigned rng_next(Rng *r) {
    r->s = r->s * 1103515245u + 12345u;
    return (r->s >> 16) & 0x7fffu;
}

static int rng_range(Rng *r, int n) { return (int)(rng_next(r) % (unsigned)n); }

typedef struct { char *s; size_t len, cap; } Buf;

static void buf_printf(Buf *b, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, tmp, n + 1);
    b->len += n;
}

/* ---------- program generator ---------- */

#define NI 4        /* int vars v0..v3 */
#define NF 2        /* float vars f0..f1 */
#define NLOOPS 6    /* loop counters c0..c5 */

static void gen_int_expr(Rng *r, Buf *b, int depth) {
    if (depth == 0 || rng_range(r, 3) == 0) {
        if (rng_range(r, 2)) buf_printf(b, "v%d", rng_range(r, NI));
        else buf_printf(b, "%d", rng_range(r, 10));
        return;
    }
    int k = rng_range(r, 4);   /* + - * / */
    buf_printf(b, "(");
    gen_int_expr(r, b, depth - 1);
    if (k == 3) {
        buf_printf(b, " / %d", 1 + rng_range(r, 9));   /* nonzero literal divisor */
    } else {
        buf_printf(b, " %c ", "+-*"[k]);
        gen_int_expr(r, b, depth - 1);
    }
    buf_printf(b, ")");
}

static void gen_float_expr(Rng *r, Buf *b, int depth) {
    if (depth == 0 || rng_range(r, 3) == 0) {
        if (rng_range(r, 2)) buf_printf(b, "f%d", rng_range(r, NF));
        else buf_printf(b, rng_range(r, 2) ? "%d.5" : "%d.0", rng_range(r, 10));
        return;
    }
    int k = rng_range(r, 4);
    buf_printf(b, "(");
    gen_float_expr(r, b, depth - 1);
    if (k == 3) {
        buf_printf(b, " / %d.0", 1 + rng_range(r, 9));
    } else {
        buf_printf(b, " %c ", "+-*"[k]);
        gen_float_expr(r, b, depth - 1);
    }
    buf_printf(b, ")");
}

static void gen_cond(Rng *r, Buf *b) {
    static const char *ops[] = { "<", ">", "<=", ">=", "==" };
    const char *op = ops[rng_range(r, 5)];
    if (rng_range(r, 3) == 0) {
        gen_float_expr(r, b, 1);
        buf_printf(b, " %s ", op);
        gen_float_expr(r, b, 1);
    } else {
        gen_int_expr(r, b, 2);
        buf_printf(b, " %s ", op);
        gen_int_expr(r, b, 2);
    }
}

static void gen_stmt(Rng *r, Buf *b, int depth, int *loops);

static void gen_block(Rng *r, Buf *b, int depth, int *loops) {
    int n = 1 + rng_range(r, 3);
    for (int i = 0; i < n; i++) gen_stmt(r, b, depth, loops);
}

static void gen_stmt(Rng *r, Buf *b, int depth, int *loops) {
    int k = rng_range(r, depth < 2 ? 7 : 3);
    if (k <= 1) {
        buf_printf(b, "v%d = ", rng_range(r, NI));
        gen_int_expr(r, b, 3);
        buf_printf(b, ";\n");
    } else if (k == 2) {
        buf_printf(b, "f%d = ", rng_range(r, NF));
        if (rng_range(r, 3) == 0) gen_int_expr(r, b, 2);   /* int -> float widening */
        else gen_float_expr(r, b, 3);
        buf_printf(b, ";\n");
    } else if (k <= 4) {
        buf_printf(b, "if (");
        gen_cond(r, b);
        buf_printf(b, ") {\n");
        gen_block(r, b, depth + 1, loops);
        if (rng_range(r, 2)) {
            buf_printf(b, "} else {\n");
            gen_block(r, b, depth + 1, loops);
        }
        buf_printf(b, "}\n");
    } else if (*loops < NLOOPS) {
        int id = (*loops)++;
        buf_printf(b, "c%d = 0;\nwhile (c%d < %d) {\n", id, id, 1 + rng_range(r, 6));
        gen_block(r, b, depth + 1, loops);
        buf_printf(b, "c%d = c%d + 1;\n}\n", id, id);
    } else {
        buf_printf(b, "v%d = ", rng_range(r, NI));
        gen_int_expr(r, b, 2);
        buf_printf(b, ";\n");
    }
}

char *fuzz_generate_program(unsigned seed, int chunks) {
    Rng r = { seed * 2654435761u + 1u };
    Buf b = { NULL, 0, 0 };
    buf_printf(&b, "");   /* allocate */

    for (int i = 0; i < NI; i++) buf_printf(&b, "int v%d;\n", i);
    for (int i = 0; i < NF; i++) buf_printf(&b, "float f%d;\n", i);
    for (int i = 0; i < NLOOPS; i++) buf_printf(&b, "int c%d;\n", i);
    for (int i = 0; i < NI; i++)
        if (rng_range(&r, 2)) buf_printf(&b, "v%d = %d;\n", i, rng_range(&r, 10));

    int loops = 0;
    for (int i = 0; i < chunks; i++) gen_stmt(&r, &b, 0, &loops);
    return b.s;
}

/* ---------- differential checker ---------- */

typedef struct { char *label; TACList *list; } Candidate;

static Env *random_env(const SymbolTable *sym, Rng *r) {
    Env *e = env_create();
    for (size_t i = 0; i < sym->count; i++) {
        const SymbolEntry *s = &sym->items[i];
        if (s->type == TYPE_FLOAT) env_set(e, s->name, value_float((double)(rng_range(r, 9) - 4) + 0.5));
        else env_set(e, s->name, value_int(rng_range(r, 9) - 4));
    }
    return e;
}

static void print_value(FILE *out, Value v) {
    if (v.kind == VAL_INT) fprintf(out, "%ld", v.i);
    else fprintf(out, "%g", v.f);
}

static void report_failure(FILE *out, unsigned seed, const char *src, const char *label,
                           const InterpResult *ref, const InterpResult *got,
                           const SymbolTable *sym) {
    fprintf(out, "FAIL seed=%u candidate=%s status=%d\n", seed, label, (int)got->status);
    for (size_t i = 0; i < sym->count; i++) {
        const char *name = sym->items[i].name;
        Value a = value_int(0), b = value_int(0);
        env_get(ref->final_env, name, &a);
        env_get(got->final_env, name, &b);
        if (a.kind != b.kind || (a.kind == VAL_INT ? a.i != b.i : a.f != b.f)) {
            fprintf(out, "  %s: expected ", name);
            print_value(out, a);
            fprintf(out, " got ");
            print_value(out, b);
            fprintf(out, "\n");
        }
    }
    fprintf(out, "--- program ---\n%s---------------\n", src);
}

/* Checks one program (given as source text) and adds to `st`. */
static void check_program(const char *src, unsigned pseed, int envs_per_program,
                          const PassInfo *extra, FILE *out, FuzzStats *st) {
    const long FUZZ_STEP_LIMIT = 200000;

    st->programs++;
    FrontEnd *fe = frontend_run(src);
    if (fe->parse_errors || fe->semantic_errors || frontend_has_temp_collision(fe->symtab)) {
        st->rejected++;
        frontend_free(fe);
        return;
    }

    /* candidates: each pass alone, fixed pipeline, all passes twice, searched, extra */
    size_t np = pass_count();
    size_t cap = np + 5;
    Candidate *cands = calloc(cap, sizeof(Candidate));
    size_t nc = 0;

    for (size_t p = 0; p < np; p++) {
        cands[nc].label = tac_dup_str(PASS_REGISTRY[p].name);
        cands[nc++].list = PASS_REGISTRY[p].fn(fe->tac);
    }
    cands[nc].label = tac_dup_str("fixed");
    cands[nc++].list = apply_fixed_pipeline(fe->tac);

    int all[2 * 16];
    int alen = 0;
    for (int rep = 0; rep < 2; rep++)
        for (size_t p = 0; p < np && alen < 2 * 16; p++) all[alen++] = (int)p;
    cands[nc].label = tac_dup_str("all-twice");
    cands[nc++].list = apply_sequence(fe->tac, all, alen);

    SearchConfig cfg = search_default_config();
    cfg.budget = 150;
    SearchResult sr = search_best_sequence(fe->tac, &cfg, NULL);
    cands[nc].label = tac_dup_str("searched");
    cands[nc++].list = tac_copy_list(sr.best);
    search_result_free(&sr);

    if (extra) {
        cands[nc].label = tac_dup_str(extra->name);
        cands[nc++].list = extra->fn(fe->tac);
    }

    Rng erng = { pseed ^ 0x9e3779b9u };
    for (int e = 0; e < envs_per_program; e++) {
        Env *env = random_env(fe->symtab, &erng);
        InterpResult ref = interp_run(fe->tac, env, FUZZ_STEP_LIMIT);
        if (ref.status != INTERP_OK) {
            st->skipped++;
            interp_result_free(&ref);
            env_free(env);
            continue;
        }
        for (size_t c = 0; c < nc; c++) {
            InterpResult got = interp_run(cands[c].list, env, FUZZ_STEP_LIMIT);
            st->checks++;
            if (!interp_equivalent(&ref, &got, fe->symtab)) {
                st->failures++;
                report_failure(out, pseed, src, cands[c].label, &ref, &got, fe->symtab);
            }
            interp_result_free(&got);
        }
        interp_result_free(&ref);
        env_free(env);
    }

    for (size_t c = 0; c < nc; c++) {
        free(cands[c].label);
        taclist_free(cands[c].list);
    }
    free(cands);
    frontend_free(fe);
}

FuzzStats fuzz_check_source(const char *source, unsigned seed, int envs_per_program,
                            const PassInfo *extra, FILE *out) {
    FuzzStats st = { 0, 0, 0, 0, 0 };
    check_program(source, seed, envs_per_program, extra, out, &st);
    return st;
}

FuzzStats fuzz_run(int n, unsigned seed, int envs_per_program,
                   const PassInfo *extra, FILE *out) {
    FuzzStats st = { 0, 0, 0, 0, 0 };
    for (int pi = 0; pi < n; pi++) {
        unsigned pseed = seed + (unsigned)pi * 7919u;
        char *src = fuzz_generate_program(pseed, 6);
        check_program(src, pseed, envs_per_program, extra, out, &st);
        free(src);
    }
    fprintf(out, "fuzz: %d programs (%d rejected by front end), %d checks, %d skipped, %d failures\n",
            st.programs, st.rejected, st.checks, st.skipped, st.failures);
    return st;
}
