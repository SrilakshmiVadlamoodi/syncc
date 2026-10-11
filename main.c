#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"
#include "optimizer.h"
#include "error_engine.h"
#include "cost.h"
#include "explain.h"
#include "fuzz.h"
#include "heuristic.h"
#include "interp.h"
#include "passes.h"
#include "pipeline.h"
#include "search.h"
#include "tacutil.h"
#include "timing.h"

typedef struct {
    const char *path;
    int search, use_cache, learn, explain, stats, timing;
    int budget;
    const char *cache_path;
    int fuzz_n, gen_n, loo;
    unsigned seed;
    const char **loo_files;
    int loo_count;
} Options;

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] <source.mini>\n"
        "       %s --fuzz N [--seed S]\n"
        "       %s --gen N [--seed S]\n"
        "       %s --loo <a.mini> <b.mini> ...\n"
        "options: --search --budget N --learn --use-cache --cache PATH\n"
        "         --explain --stats --time\n",
        prog, prog, prog, prog);
}

static int parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof *o);
    o->seed = 1;
    o->cache_path = ".minicc_cache";
    o->loo_files = malloc(sizeof(char *) * (size_t)(argc + 1));
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--search")) o->search = 1;
        else if (!strcmp(a, "--learn")) o->learn = 1;
        else if (!strcmp(a, "--use-cache")) o->use_cache = 1;
        else if (!strcmp(a, "--explain")) o->explain = 1;
        else if (!strcmp(a, "--stats")) o->stats = 1;
        else if (!strcmp(a, "--time")) o->timing = 1;
        else if (!strcmp(a, "--loo")) o->loo = 1;
        else if (!strcmp(a, "--budget") && i + 1 < argc) o->budget = atoi(argv[++i]);
        else if (!strcmp(a, "--cache") && i + 1 < argc) o->cache_path = argv[++i];
        else if (!strcmp(a, "--seed") && i + 1 < argc) o->seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--fuzz") && i + 1 < argc) o->fuzz_n = atoi(argv[++i]);
        else if (!strcmp(a, "--gen") && i + 1 < argc) o->gen_n = atoi(argv[++i]);
        else if (a[0] == '-' && a[1] == '-') { fprintf(stderr, "error: unknown option %s\n", a); return 0; }
        else if (o->loo) o->loo_files[o->loo_count++] = a;
        else if (!o->path) o->path = a;
        else return 0;
    }
    return 1;
}

static char *read_file(const char *path) {
    char *buf = pipeline_read_file(path);
    if (!buf) {
        fprintf(stderr, "error: could not open %s\n", path);
        exit(1);
    }
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

static void stat_row(const char *label, TACList *list) {
    long dyn = interp_dynamic_cost(list, NULL);
    printf("STAT %s %ld %ld\n", label, dyn, cost_static_ops(list));
}

static void verify_row(const char *label, TACList *ref, TACList *cand, const SymbolTable *symtab) {
    InterpResult a = interp_run(ref, NULL, INTERP_DEFAULT_STEP_LIMIT);
    InterpResult b = interp_run(cand, NULL, INTERP_DEFAULT_STEP_LIMIT);
    if (a.status != INTERP_OK)
        printf("VERIFY %s skipped (original does not run to completion)\n", label);
    else
        printf("VERIFY %s %s\n", label, interp_equivalent(&a, &b, symtab) ? "ok" : "MISMATCH");
    interp_result_free(&a);
    interp_result_free(&b);
}

int main(int argc, char **argv) {
    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 1;
    }

    if (opt.loo) {
        int n = heuristic_leave_one_out(opt.loo_files, opt.loo_count,
                                        CACHE_DEFAULT_MAX_DISTANCE, stdout);
        free(opt.loo_files);
        return n > 0 ? 0 : 1;
    }
    if (opt.gen_n > 0) {
        char *src = fuzz_generate_program(opt.seed, opt.gen_n);
        fputs(src, stdout);
        free(src);
        free(opt.loo_files);
        return 0;
    }
    if (opt.fuzz_n > 0) {
        FuzzStats fs = fuzz_run(opt.fuzz_n, opt.seed, 4, NULL, stdout);
        free(opt.loo_files);
        return fs.failures > 0 ? 1 : 0;
    }

    if (!opt.path) {
        usage(argv[0]);
        return 1;
    }

    char *source = read_file(opt.path);

    /* --- Stage 1: Lexing --- */
    double t0 = now_seconds();
    TokenList *tokens = lex(source);
    double t_lex = now_seconds() - t0;
    printf("=== Tokens ===\n");
    for (size_t i = 0; i < tokens->count; i++) {
        Token *t = &tokens->items[i];
        printf("  %-12s '%s'  (line %d, col %d)\n",
               token_type_name(t->type), t->lexeme, t->line, t->col);
    }

    /* --- Stage 2: Parsing --- */
    codegen_reset_counters();
    ParseErrorList *parse_errors = parse_error_list_create();
    t0 = now_seconds();
    AstNode *ast = parse_program(tokens, parse_errors);
    double t_parse = now_seconds() - t0;

    printf("\n=== AST ===\n");
    ast_print(ast, 0);

    /* --- Stage 3: Semantic analysis --- */
    SemanticErrorList *semantic_errors = semantic_error_list_create();
    t0 = now_seconds();
    SymbolTable *symtab = analyze(ast, semantic_errors);
    double t_sem = now_seconds() - t0;

    printf("\n=== Symbol Table ===\n");
    symtab_print(symtab, stdout);

    /* --- Stage 4: TAC generation --- */
    t0 = now_seconds();
    TACList *tac = generate_tac(ast);
    double t_codegen = now_seconds() - t0;
    printf("\n=== TAC (unoptimized) ===\n");
    tac_print(tac, stdout);

    /* --- Stage 5: Optimization (constant folding, then LICM) --- */
    int collision = frontend_has_temp_collision(symtab);
    double t_fold = 0.0, t_licm = 0.0;
    TACList *tac_opt, *tac_licm;
    if (collision) {
        fprintf(stderr, "warning: a variable is named like a compiler temporary (t<N>); "
                        "optimizations are disabled for this program\n");
        tac_opt = tac_copy_list(tac);
        tac_licm = tac_copy_list(tac);
    } else {
        t0 = now_seconds();
        tac_opt = optimize_constant_folding(tac);
        t_fold = now_seconds() - t0;
        t0 = now_seconds();
        tac_licm = optimize_loop_invariant_code_motion(tac_opt);
        t_licm = now_seconds() - t0;
    }
    printf("\n=== TAC (optimized: constant folding) ===\n");
    tac_print(tac_opt, stdout);
    printf("\n=== TAC (optimized: + loop-invariant code motion) ===\n");
    tac_print(tac_licm, stdout);

    /* --- Stage 5b: search / cache / explain / stats (only with flags) --- */
    TACList *chosen = tac_licm;
    const char *chosen_label = "fixed";
    SearchResult sr;
    memset(&sr, 0, sizeof sr);
    int have_sr = 0;
    double t_search = 0.0;

    if (!collision && (opt.search || opt.learn)) {
        SearchConfig cfg = search_default_config();
        if (opt.budget > 0) cfg.budget = opt.budget;
        t0 = now_seconds();
        sr = search_best_sequence(tac, &cfg, NULL);
        t_search = now_seconds() - t0;
        have_sr = 1;
        chosen = sr.best;
        chosen_label = "searched";
        printf("\n=== TAC (searched: ");
        search_print_sequence(&sr, stdout);
        printf(") ===\n");
        tac_print(chosen, stdout);
    }

    if (!collision && opt.learn && have_sr) {
        HeuristicCache *cache = cache_load(opt.cache_path);
        Features ft = heuristic_features(tac);
        double ratio = sr.baseline_cost > 0 ? (double)sr.best_cost / (double)sr.baseline_cost : 1.0;
        cache_learn(cache, &ft, sr.sequence, sr.sequence_len, ratio);
        if (cache_save(cache, opt.cache_path))
            printf("\ncache: learned entry saved to %s (%zu entries)\n", opt.cache_path, cache->count);
        else
            fprintf(stderr, "warning: could not write cache file %s\n", opt.cache_path);
        cache_free(cache);
    }

    TACList *cached_result = NULL;
    int cached_seq[SEARCH_MAX_SEQ];
    int cached_len = -1;   /* >= 0 once a cached sequence was applied */
    if (!collision && opt.use_cache && !have_sr) {
        HeuristicCache *cache = cache_load(opt.cache_path);
        Features ft = heuristic_features(tac);
        int dist = 0;
        const CacheEntry *e = cache_lookup(cache, &ft, CACHE_DEFAULT_MAX_DISTANCE, &dist);
        if (e) {
            cached_result = apply_sequence(tac, e->seq, e->len);
            long cached_cost = interp_dynamic_cost(cached_result, NULL);
            long fixed_cost = interp_dynamic_cost(tac_licm, NULL);
            if (cached_cost < 0 || (fixed_cost >= 0 && cached_cost > fixed_cost)) {
                /* never worse than the baseline */
                taclist_free(cached_result);
                cached_result = NULL;
                printf("\ncache: hit (distance %d) did not beat the fixed pipeline, using it\n", dist);
                cache_free(cache);
                goto cache_done;
            }
            chosen = cached_result;
            cached_len = e->len;
            memcpy(cached_seq, e->seq, sizeof(int) * (size_t)e->len);
            chosen_label = "cached";
            printf("\n=== TAC (cache %s, distance %d: ", dist == 0 ? "exact" : "nearest", dist);
            if (e->len == 0) printf("(none)");
            for (int i = 0; i < e->len; i++)
                printf("%s%s", i ? ", " : "", PASS_REGISTRY[e->seq[i]].name);
            printf(") ===\n");
            tac_print(chosen, stdout);
        } else {
            printf("\ncache: miss, using the fixed pipeline\n");
        }
        cache_free(cache);
    }
cache_done:;
    if (!collision && opt.explain) {
        printf("\n=== Explain (%s) ===\n", chosen_label);
        if (have_sr) {
            explain_sequence(tac, sr.sequence, sr.sequence_len, NULL, stdout);
        } else if (cached_len >= 0) {
            explain_sequence(tac, cached_seq, cached_len, NULL, stdout);
        } else {
            int fixed[2] = { pass_index("fold"), pass_index("licm") };
            explain_sequence(tac, fixed, 2, NULL, stdout);
        }
    }

    if (!collision && opt.stats) {
        printf("\n=== Stats ===\n");
        cost_model_print(stdout);
        stat_row("unoptimized", tac);
        stat_row("fixed", tac_licm);
        verify_row("fixed", tac, tac_licm, symtab);
        if (chosen != tac_licm) {
            stat_row(chosen_label, chosen);
            verify_row(chosen_label, tac, chosen, symtab);
        }
        if (have_sr) {
            printf("STAT search_candidates %d\n", sr.candidates);
            printf("STAT search_ms %.3f\n", t_search * 1000.0);
        }
    }

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

    if (opt.timing) {
        printf("\n=== Timing (ms) ===\n");
        printf("TIME lex %.3f\n", t_lex * 1000.0);
        printf("TIME parse %.3f\n", t_parse * 1000.0);
        printf("TIME semantic %.3f\n", t_sem * 1000.0);
        printf("TIME codegen %.3f\n", t_codegen * 1000.0);
        printf("TIME fold %.3f\n", t_fold * 1000.0);
        printf("TIME licm %.3f\n", t_licm * 1000.0);
        if (have_sr) printf("TIME search %.3f\n", t_search * 1000.0);
    }

    /* --- Cleanup --- */
    if (have_sr) search_result_free(&sr);
    taclist_free(cached_result);
    taclist_free(tac_licm);
    taclist_free(tac_opt);
    taclist_free(tac);
    symtab_free(symtab);
    semantic_error_list_free(semantic_errors);
    ast_free(ast);
    parse_error_list_free(parse_errors);
    tokenlist_free(tokens);
    free(source);
    free(opt.loo_files);

    return 0;
}
